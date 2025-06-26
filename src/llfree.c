#include "llfree_inner.h"

#include "llfree.h"
#include "child.h"
#include "llfree_platform.h"
#include "tree.h"
#include "local.h"
#include "lower.h"
#include "utils.h"

/// Initializes the Tree array by reading the child counters
static void init_trees(llfree_t *self)
{
	assert(self != NULL);

	for (size_t tree_idx = 0; tree_idx < self->trees_len; ++tree_idx) {
		treeF_t sum = 0;
		for (size_t child_idx = 0; child_idx < LLFREE_TREE_CHILDREN;
		     ++child_idx) {
			child_t child =
				atom_load(&self->lower.children[tree_idx]
						   .entries[child_idx]);
			sum += child.free;
		}
		tree_kind_t kind = sum == LLFREE_TREE_SIZE ? TREE_HUGE :
							     TREE_FIXED;
		self->trees[tree_idx] = tree_new(false, kind, sum, 0);
	}
}

llfree_meta_size_t llfree_metadata_size(size_t cores, size_t frames)
{
	size_t tree_len = div_ceil(frames, LLFREE_TREE_SIZE);
	size_t tree_size =
		align_up(sizeof(tree_t) * tree_len, LLFREE_CACHE_SIZE);
	llfree_meta_size_t meta = {
		.llfree = sizeof(llfree_t),
		.trees = tree_size,
		.local = ll_local_size(cores),
		.lower = lower_metadata_size(frames),
	};
	return meta;
}

static ll_unused bool check_meta(llfree_meta_t meta, llfree_meta_size_t sizes)
{
	if (meta.local == NULL || meta.trees == NULL || meta.lower == NULL)
		return false;
	if ((size_t)meta.local % LLFREE_CACHE_SIZE != 0 ||
	    (size_t)meta.trees % LLFREE_CACHE_SIZE != 0 ||
	    (size_t)meta.lower % LLFREE_CACHE_SIZE != 0)
		return false;
	// no overlap!
	return (meta.local + sizes.local <= meta.lower ||
		meta.lower + sizes.lower <= meta.local) &&
	       (meta.local + sizes.local <= meta.trees ||
		meta.trees + sizes.trees <= meta.local) &&
	       (meta.lower + sizes.lower <= meta.trees ||
		meta.trees + sizes.trees <= meta.lower);
}

llfree_result_t llfree_init(llfree_t *self, size_t cores, size_t frames,
			    uint8_t init, llfree_meta_t meta)
{
	assert(self != NULL);
	assert(check_meta(meta, llfree_metadata_size(cores, frames)));

	if (init >= LLFREE_INIT_MAX) {
		llfree_info("Invalid init mode %d", init);
		return llfree_err(LLFREE_ERR_INIT);
	}
	if (frames < MIN_PAGES || frames > MAX_PAGES) {
		llfree_info("Invalid size %" PRIu64, (uint64_t)frames);
		return llfree_err(LLFREE_ERR_INIT);
	}

	llfree_result_t res =
		lower_init(&self->lower, frames, init, meta.lower);
	if (!llfree_is_ok(res)) {
		return res;
	}

	// check if more cores than trees -> if not shared locale data
	self->trees_len = div_ceil(frames, LLFREE_TREE_SIZE);
	self->trees = (_Atomic(tree_t) *)(meta.trees);
	self->local = (local_t *)meta.local;
	ll_local_init(self->local, LL_MIN(cores, self->trees_len));

	self->contains_huge = init != LLFREE_INIT_ALLOC;

	if (init != LLFREE_INIT_NONE)
		init_trees(self);
	return llfree_ok(0, false, false);
}

llfree_meta_t llfree_metadata(const llfree_t *self)
{
	assert(self != NULL);
	llfree_meta_t meta = {
		.local = (uint8_t *)self->local,
		.trees = (uint8_t *)self->trees,
		.lower = lower_metadata(&self->lower),
	};
	return meta;
}

/// Search function callback
typedef llfree_result_t (*search_fn)(llfree_t *self, size_t idx, void *args);

/// Searches the tree array for a tree with a free counter in the provided range,
/// and executes the callback for it until it stops returning ERR_MEMORY.
static llfree_result_t search(llfree_t *self, uint64_t start, uint64_t offset,
			      uint64_t len, search_fn callback, void *args)
{
	int64_t base = (int64_t)(start + self->trees_len);
	for (int64_t i = (int64_t)offset; i < (int64_t)len; ++i) {
		// Search alternating left and right from start
		int64_t off = i % 2 == 0 ? i / 2 : -((i + 1) / 2);
		uint64_t idx = (uint64_t)(base + off) % self->trees_len;
		llfree_result_t res = callback(self, idx, args);

		if (res.error != LLFREE_ERR_MEMORY) {
			if (i - offset > 4)
				llfree_debug(
					"large global search o=%" PRIu64
					" i=%" PRIu64 ": %" PRIu64 " -> %d",
					offset, i, len,
					llfree_is_ok(res) ?
						tree_from_frame(res.frame) :
						-1);
			return res;
		}
	}
	if (len > 4)
		llfree_debug("large global search failed o=%" PRIu64
			     " i=%" PRIu64 ": %" PRIu64,
			     offset, len, start + len);

	return llfree_err(LLFREE_ERR_MEMORY);
}

/// Swap out the currently reserved tree for a new one and writes back the free
/// counter to the previously reserved global tree.
///
/// This checks first if the reserving flag has the expected value
/// returning false if not.
static void swap_reserved(llfree_t *self, size_t core, size_t new_idx,
			  tree_t new, tree_change_t previous_change)
{
	llfree_debug("swap c=%zu idx=%zu kind=%s", core, new_idx,
		     tree_kind_name(tree_kind(new.kind)));
	local_result_t res =
		ll_local_swap(self->local, core, previous_change, new_idx, new);
	assert(res.success);
	if (res.present) {
		tree_change_t change = tree_change(
			previous_change.kind, res.tree.free, res.tree.zeroed);
		tree_t tree;
		atom_update(&self->trees[tree_from_row(res.start_row)], tree,
			    tree_unreserve, change);
	}
}

typedef struct reserve_args {
	size_t core;
	llflags_t flags;
	p_range_t free;
} reserve_args_t;

/// Reserves a new tree and allocates with it.
/// Only if the allocation succeeds, the tree is fully reserved.
/// Otherwise the reservation is reverted.
static llfree_result_t reserve_tree_and_get(llfree_t *self, size_t idx,
					    void *args)
{
	assert(idx < self->trees_len);

	reserve_args_t *rargs = (reserve_args_t *)args;
	llflags_t flags = rargs->flags;
	treeF_t max = rargs->free.max;
	treeF_t min =
		LL_MAX(rargs->free.min, (treeF_t)(1 << rargs->flags.order));
	if (max < min)
		return llfree_err(LLFREE_ERR_MEMORY);

	tree_kind_t kind = tree_kind_flags(flags);
	tree_change_t change = tree_change(kind, min, rargs->flags.zeroed);

	tree_t old;
	if (!atom_update(&self->trees[idx], old, tree_reserve, change, max))
		return llfree_err(LLFREE_ERR_MEMORY);
	assert(!old.reserved && old.free >= min);

	llfree_result_t res =
		lower_get(&self->lower, frame_from_tree(idx), flags);

	if (llfree_is_ok(res)) {
		tree_t new = old;
		bool success = tree_get(&new, tree_change_flags(flags));
		assert(success);
		new.reserved = true;
		swap_reserved(self, rargs->core, idx, new, change);
	} else {
		llfree_debug("reserve failed c=%zu idx=%zu kind=%s",
			     rargs->core, idx, tree_kind_name(kind));
		// undo reservation
		tree_t tree;
		atom_update(&self->trees[idx], tree, tree_unreserve,
			    tree_change(kind, old.free, old.zeroed));
	}
	return res;
}

/// Reserves a new tree and allocates from it.
///
/// The search for a new tree aims to be both fast and avoid fragmentation.
static llfree_result_t reserve_and_get(llfree_t *self, size_t core,
				       llflags_t flags, local_result_t old)
{
	const size_t cl_trees = LLFREE_CACHE_SIZE / sizeof(tree_t);

	llfree_result_t res;

	llfree_debug("reserve c=%zu o=%d kind=%s (z=%d)", core, flags.order,
		     tree_kind_name(tree_kind_flags(flags)), flags.zeroed);
	uint64_t start = old.present ?
				 tree_from_row(old.start_row) :
				 (self->trees_len / llfree_cores(self) * core);
	assert(start < self->trees_len);
	start = align_down(start, cl_trees);

	size_t near = (self->trees_len / llfree_cores(self)) / 4;
	near = LL_MIN(LL_MAX(near, cl_trees / 4), cl_trees * 2);

	reserve_args_t args = { .core = core,
				.flags = flags,
				.free = { 0, 0 } };

	if (flags.order < LLFREE_HUGE_ORDER) {
		// Over half filled trees
		args.free = (p_range_t){ LLFREE_TREE_SIZE / 16,
					 LLFREE_TREE_SIZE / 2 };
		res = search(self, start, 1, near, reserve_tree_and_get, &args);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;

		// Partially filled tree
		args.free = (p_range_t){ LLFREE_TREE_SIZE / 64,
					 LLFREE_TREE_SIZE -
						 (LLFREE_TREE_SIZE / 16) };
		res = search(self, start, 1, 2 * near, reserve_tree_and_get,
			     &args);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;
	}

	// Not free tree
	args.free = (p_range_t){ 0, LLFREE_TREE_SIZE - 1 };
	res = search(self, start, 1, near, reserve_tree_and_get, &args);
	if (res.error != LLFREE_ERR_MEMORY)
		return res;

	// Any tree
	args.free = (p_range_t){ 0, LLFREE_TREE_SIZE };
	res = search(self, start, 0, self->trees_len, reserve_tree_and_get,
		     &args);

	return res;
}

/// Synchronizes the free_counter of given local with the global counter.
static bool ll_unused sync_with_global(llfree_t *self, size_t core,
				       tree_change_t change, local_result_t old)
{
	assert(self != NULL);
	assert(old.present);
	assert(old.tree.kind == change.kind.id);

	/// Subtract local free counters
	tree_change_t steal_change;
	if (change.kind.id == TREE_HUGE.id) {
		if (huge_from_frame(old.tree.free) >= change.huge)
			return false;
		steal_change = tree_change_huge(
			change.huge - huge_from_frame(old.tree.free),
			change.zeroed > old.tree.zeroed ?
				change.zeroed - old.tree.zeroed :
				0);
	} else {
		if (old.tree.free >= change.frames)
			return false;
		steal_change =
			tree_change_small(change.frames - old.tree.free,
					  change.kind.id == TREE_MOVABLE.id);
	}

	size_t tree_idx = tree_from_row(old.start_row);

	tree_t old_tree;
	if (!atom_update(&self->trees[tree_idx], old_tree, tree_steal_counter,
			 steal_change))
		return false;

	assert(old_tree.kind == old.tree.kind);

	tree_change_t stolen_change = tree_change(
		tree_kind(old_tree.kind), old_tree.free, old_tree.zeroed);
	if (!ll_local_put(self->local, core, stolen_change, tree_idx)) {
		// Revert the steal
		atom_update(&self->trees[tree_idx], old_tree, tree_put,
			    stolen_change);
		return false;
	}
	return true;
}

static llfree_result_t get_from_local(llfree_t *self, size_t core,
				      llflags_t flags, local_result_t *old)
{
	// Try decrementing the local counter
	tree_change_t change = tree_change_flags(flags);
	*old = ll_local_get(self->local, core, change, optional_size_none());
	if (old->success) {
		llfree_result_t res = lower_get(
			&self->lower, frame_from_row(old->start_row), flags);
		if (llfree_is_ok(res)) {
			size_t start_row = row_from_frame(res.frame);
			// Update local start index
			if (old->start_row != start_row) {
				*old = ll_local_set_start(self->local, core,
							  change, start_row);
			}
			return res;
		}

		// Undo decrement (inc global tree)
		tree_t tree;
		size_t tree_idx = tree_from_row(old->start_row);
		atom_update(&self->trees[tree_idx], tree, tree_put, change);

		return res;
	}

	// Try synchronizing with global counter
	if (old->present && sync_with_global(self, core, change, *old)) {
		// Success -> Retry allocation
		return llfree_err(LLFREE_ERR_RETRY);
	}

	return llfree_err(LLFREE_ERR_MEMORY);
}

/// Search and steal from any reserved tree
static llfree_result_t steal_from_reserved(llfree_t *self, size_t core,
					   llflags_t flags,
					   optional_size_t tree_idx)
{
	local_result_t res;
	tree_change_t change = tree_change_flags(flags);

	llfree_debug("steal c=%zu o=%d kind=%s", core, flags.order,
		     tree_kind_name(tree_kind_flags(flags)));

	// Try stealing from lower tree first (they do not have to be demoted)
	res = ll_local_steal(self->local, core, change, false, tree_idx);
	if (res.success) {
		llfree_result_t res2 = lower_get(
			&self->lower, frame_from_row(res.start_row), flags);
		if (llfree_is_ok(res2) || res2.error != LLFREE_ERR_MEMORY)
			return res2;

		// Undo decrement (inc global tree)
		tree_t tree;
		atom_update(&self->trees[tree_from_row(res.start_row)], tree,
			    tree_put, change);
	}

	llfree_debug("steal c=%zu o=%d kind=%s: no lower tree found", core,
		     flags.order, tree_kind_name(tree_kind_flags(flags)));

	// Fallback to higher trees, which have to be demoted
	res = ll_local_steal(self->local, core, change, true, tree_idx);
	if (res.success) {
		llfree_debug("steal c=%zu o=%d kind=%s: found higher tree",
			     core, flags.order,
			     tree_kind_name(tree_kind_flags(flags)));

		// Try to get the tree from the global trees
		llfree_result_t res2 = lower_get(
			&self->lower, frame_from_row(res.start_row), flags);
		if (llfree_is_ok(res2) || res2.error != LLFREE_ERR_MEMORY) {
			llfree_debug("Local demote...");
			local_result_t stolen =
				ll_local_demote(self->local, core, change,
						tree_from_row(res.start_row));
			if (!stolen.success)
				llfree_warn("Local demote failed!");

			if (stolen.success && stolen.present) {
				swap_reserved(self, core,
					      tree_from_row(stolen.start_row),
					      stolen.tree, change);
			}

			// Also demote global tree
			tree_t tree;
			atom_update(
				&self->trees[tree_from_row(stolen.start_row)],
				tree, tree_demote, tree_kind(stolen.tree.kind));
			return res2;
		}

		// Undo decrement (inc global tree)
		tree_t tree;
		atom_update(&self->trees[tree_from_row(res.start_row)], tree,
			    tree_put, change);
	}

	return llfree_err(LLFREE_ERR_MEMORY);
}

typedef struct alloc_global_args {
	llflags_t flags;
} alloc_global_args_t;

static llfree_result_t alloc_global_tree(llfree_t *self, size_t idx, void *args)
{
	assert(idx < self->trees_len);

	alloc_global_args_t *rargs = args;
	llflags_t flags = rargs->flags;

	tree_t old;
	tree_change_t change = tree_change_flags(flags);
	if (!atom_update(&self->trees[idx], old, tree_get, change))
		return llfree_err(LLFREE_ERR_MEMORY);

	assert(old.free >= (1 << flags.order));

	llfree_result_t res =
		lower_get(&self->lower, frame_from_tree(idx), flags);

	if (!llfree_is_ok(res)) {
		// undo reservation
		tree_t expected = old;
		tree_get(&expected, change);
		// first try to also reset the tree kind if nothing has changed
		if (!atom_cmp_exchange(&self->trees[idx], &expected, old)) {
			// keep tree kind as other cpu might have already used it
			atom_update(&self->trees[idx], expected, tree_put,
				    change);
		}
	}
	return res;
}

static llfree_result_t alloc_any_global(llfree_t *self, size_t start,
					llflags_t flags)
{
	alloc_global_args_t args = { .flags = flags };
	return search(self, start, 0, self->trees_len, alloc_global_tree,
		      &args);
}

llfree_result_t llfree_get(llfree_t *self, size_t core, llflags_t flags)
{
	assert(self != NULL);
	assert(flags.order <= LLFREE_MAX_ORDER);
	flags.zeroed = flags.zeroed && flags.order == LLFREE_CHILD_ORDER;
	core = core % llfree_cores(self);

	bool contains_zeroed = false;
	bool contains_huge = false;
	// Skip search for non-existing trees
	if (flags.order >= LLFREE_CHILD_ORDER) {
		contains_zeroed = atom_load(&self->contains_zeroed);
		contains_huge = atom_load(&self->contains_huge);
		if (contains_zeroed)
			flags.zeroed = true;
		else if (contains_huge)
			flags.zeroed = false;
	}

	local_result_t old = local_result(
		false, false, 0, tree_new(false, tree_kind_flags(flags), 0, 0));
	llfree_result_t res;

	bool has_locals = self->trees_len >
			  ((TREE_KINDS + 1) * llfree_cores(self));
	if (has_locals) {
retry:
		for (size_t i = 0; i < RETRIES; i++) {
			res = get_from_local(self, core, flags, &old);
			if (res.error != LLFREE_ERR_RETRY)
				break;
		}
		if (res.error != LLFREE_ERR_MEMORY &&
		    res.error != LLFREE_ERR_RETRY)
			return res;

		// reserve new tree
		res = reserve_and_get(self, core, flags, old);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;

		// take a huge frame from the other reserved trees
		for (size_t i = 0; i < RETRIES; i++) {
			res = steal_from_reserved(self, core, flags,
						  optional_size_none());
			if (res.error != LLFREE_ERR_RETRY)
				break;
		}
		if (res.error != LLFREE_ERR_MEMORY &&
		    res.error != LLFREE_ERR_RETRY)
			return res;

		// Update search flags and maybe retry
		if (res.error == LLFREE_ERR_MEMORY &&
		    flags.order == LLFREE_CHILD_ORDER) {
			llfree_debug("retry kind=%s",
				     tree_kind_name(tree_kind_flags(flags)));
			// The next allocations should skip searching
			if (flags.zeroed) {
				contains_zeroed = false;
				atom_store(&self->contains_zeroed,
					   contains_zeroed);
			} else {
				contains_huge = false;
				atom_store(&self->contains_huge, contains_huge);
			}
			flags.zeroed = !flags.zeroed;

			if (flags.zeroed ? contains_zeroed : contains_huge)
				goto retry;
		}
	}

	// just take from any tree
	// TODO: we might want to have another start index
	size_t idx = old.present ? tree_from_row(old.start_row) : 0;
	flags.zeroed = false;
	return alloc_any_global(self, idx, flags);
}

llfree_result_t llfree_get_at(llfree_t *self, size_t core, uint64_t frame,
			      llflags_t flags)
{
	llfree_result_t res;

	assert(self != NULL);
	assert(flags.order <= LLFREE_MAX_ORDER);
	assert(frame < self->lower.frames);
	core = core % llfree_cores(self);

	size_t tree_idx = tree_from_frame(frame);

	tree_change_t change = tree_change_flags(flags);

	bool has_locals = self->trees_len >
			  ((TREE_KINDS + 1) * llfree_cores(self));

	for (size_t r = 0; r < RETRIES; r++) {
		// Decrement reserved
		if (has_locals) {
			local_result_t old_r;
			old_r = ll_local_get(self->local, core, change,
					     optional_size(tree_idx));
			if (old_r.success)
				goto search_found;
		}

		// Decrement global
		tree_t old_t;
		if (atom_update(&self->trees[tree_idx], old_t, tree_get,
				change)) {
			flags.zeroed = flags.zeroed && old_t.zeroed;
			goto search_found;
		}

		if (!has_locals)
			continue;

		res = steal_from_reserved(self, core, flags,
					  optional_size(tree_idx));
		if (llfree_is_ok(res))
			return res;

		if (res.error == LLFREE_ERR_MEMORY)
			break;
		if (res.error != LLFREE_ERR_RETRY)
			return res;
	}
	// Search failed...
	llfree_info("get_at dec failed %" PRIu64, frame);
	return llfree_err(LLFREE_ERR_MEMORY);

search_found:
	res = lower_get_at(&self->lower, frame, flags);
	if (!llfree_is_ok(res)) {
		// Increment global to prevent race conditions
		tree_t tree;
		atom_update(&self->trees[tree_idx], tree, tree_put, change);
	}
	return res;
}

llfree_result_t llfree_put(llfree_t *self, size_t core, uint64_t frame,
			   llflags_t flags)
{
	assert(self != NULL);
	assert(flags.order <= LLFREE_MAX_ORDER);
	assert(frame < self->lower.frames);
	core = core % llfree_cores(self);

	llfree_result_t res = lower_put(&self->lower, frame, flags);
	if (!llfree_is_ok(res)) {
		llfree_info("lower err %" PRIu64, (uint64_t)res.error);
		return res;
	}

	// Frame is successfully freed in lower allocator

	size_t tree_idx = tree_from_frame(frame);

	// Update free-reserve heuristic
	bool reserve = LLFREE_ENABLE_FREE_RESERVE &&
		       ll_local_free_inc(self->local, core, tree_idx);

	tree_t old;
	tree_change_t change = tree_change_flags(flags);

	// Update reservation if present
	if (ll_local_put(self->local, core, change, tree_idx)) {
		reserve = false;
	} else {
		// Update global tree or reserve
		atom_update(&self->trees[tree_idx], old, tree_put_or_reserve,
			    change, &reserve, TREE_LOWER_LIM);
	}

	if (flags.order >= LLFREE_CHILD_ORDER) {
		if (flags.zeroed && !atom_load(&self->contains_zeroed))
			atom_store(&self->contains_zeroed, true);
		if (!flags.zeroed && !atom_load(&self->contains_huge))
			atom_store(&self->contains_huge, true);
	}

	// Free-reserve heuristic:
	// Reserve trees where a lot of frees happen, assuming locality
	if (reserve) {
		tree_t new = old;
		bool success = tree_put(&new, change);
		assert(success);
		// fully reserve the new tree
		new.reserved = true;
		swap_reserved(self, core, tree_idx, new, change);
	}
	return llfree_err(LLFREE_ERR_OK);
}

llfree_result_t llfree_drain(llfree_t *self, size_t core)
{
	ll_local_drain(self->local, core);
	return llfree_err(LLFREE_ERR_OK);
}

size_t llfree_cores(const llfree_t *self)
{
	assert(self != NULL);
	return ll_local_cores(self->local);
}

size_t llfree_frames(const llfree_t *self)
{
	assert(self != NULL);
	return self->lower.frames;
}

size_t llfree_huge(const llfree_t *self)
{
	assert(self != NULL);
	return div_ceil(self->lower.frames, 1 << LLFREE_HUGE_ORDER);
}

ll_stats_t llfree_stats(const llfree_t *self)
{
	assert(self != NULL);

	// Local counters
	ll_stats_t stats = ll_local_stats(self->local);
	stats.frames = self->lower.frames;
	stats.huge = div_ceil(self->lower.frames, 1 << LLFREE_HUGE_ORDER);

	// Global counters
	for (size_t i = 0; i < self->trees_len; i++) {
		tree_t t = atom_load(&self->trees[i]);
		stats.free_frames += t.free;
		if (t.kind == TREE_HUGE.id) {
			stats.free_huge += t.free >> LLFREE_HUGE_ORDER;
			stats.zeroed_huge += t.zeroed;
		}
	}
	return stats;
}

ll_stats_t llfree_full_stats(const llfree_t *self)
{
	assert(self != NULL);
	return lower_stats(&self->lower);
}

ll_stats_t llfree_stats_at(const llfree_t *self, uint64_t frame, size_t order)
{
	assert(self != NULL);
	if (order == LLFREE_TREE_ORDER) {
		ll_stats_t stats = {
			LLFREE_TREE_SIZE, LLFREE_TREE_CHILDREN, 0, 0, 0, 0
		};
		tree_t tree = atom_load(&self->trees[tree_from_frame(frame)]);
		stats.free_frames = tree.free;
		if (tree.kind == TREE_HUGE.id) {
			stats.free_huge = tree.free >> LLFREE_HUGE_ORDER;
			stats.zeroed_huge = tree.zeroed;
		}
		if (tree.reserved) {
			ll_stats_t local_stats =
				ll_local_stats_at(self->local, frame);
			stats.free_frames += local_stats.free_frames;
			stats.free_huge += local_stats.free_huge;
			stats.zeroed_huge += local_stats.zeroed_huge;
		}
		return stats;
	}
	return lower_stats_at(&self->lower, frame, order);
}

ll_stats_t llfree_full_stats_at(const llfree_t *self, uint64_t frame,
				size_t order)
{
	assert(self != NULL);
	return lower_stats_at(&self->lower, frame, order);
}

struct try_reclaim_args {
	/// Mark the reclaimed frame as allocated
	bool alloc;
	/// Only reclaim frames that are not reclaimed yet
	bool not_reclaimed;
	/// Only reclaim non-zeroed frames
	bool not_zeroed;
};

static llfree_result_t try_reclaim(llfree_t *self, size_t idx, void *args)
{
	assert(idx < self->trees_len);
	assert(args != NULL);
	struct try_reclaim_args *rargs = (struct try_reclaim_args *)args;
	bool alloc = rargs->alloc;
	bool not_zeroed = rargs->not_zeroed;
	bool not_reclaimed = rargs->not_reclaimed;

	bool success = false;
	tree_t old;
	atom_update(&self->trees[idx], old, tree_reclaim, &success, not_zeroed,
		    alloc);
	if (!success)
		return llfree_err(LLFREE_ERR_MEMORY);

	if (alloc) {
		tree_t tmp = atom_load(&self->trees[idx]);
		assert(tmp.free < old.free);
		assert(tmp.zeroed <= old.zeroed);
	}

	llfree_result_t res = lower_reclaim(&self->lower,
					    idx << LLFREE_TREE_ORDER, alloc,
					    not_reclaimed, not_zeroed);

	if (!llfree_is_ok(res)) {
		// If we changed the tree, undo this transaction
		if (alloc || (not_zeroed && old.kind == TREE_HUGE.id)) {
			bool changed = atom_update(&self->trees[idx], old, tree_undo_reclaim,
				    not_zeroed, alloc);
			assert(!alloc || changed);
		}
	}
	return res;
}

llfree_result_t llfree_reclaim(llfree_t *self, size_t core, bool alloc,
			       bool not_reclaimed, bool not_zeroed)
{
	core = core % llfree_cores(self);
	size_t start_idx = ll_local_reclaimed(self->local, core);

	// Search and allocate
	llfree_result_t ret;
	// First try to reclaim non-zeroed frames
	struct try_reclaim_args args = { .alloc = alloc,
					 .not_reclaimed = not_reclaimed,
					 .not_zeroed = true };
	ret = search(self, start_idx, 0, self->trees_len, try_reclaim, &args);

	// Update the local reclaim index
	if (llfree_is_ok(ret) && start_idx != tree_from_frame(ret.frame)) {
		llfree_debug("Set reclaim_idx %" PRIuS " -> %" PRIuS, start_idx,
			     tree_from_frame(ret.frame));
		ll_local_set_reclaimed(self->local, core,
				       tree_from_frame(ret.frame));
	}
	return ret;
}

llfree_result_t llfree_return(llfree_t *self, uint64_t frame, bool install)
{
	llfree_result_t res = lower_return(&self->lower, frame, install);
	if (llfree_is_ok(res)) {
		tree_t old;
		size_t idx = tree_from_frame(frame);
		tree_change_t change = tree_change_huge(1, 1);
		atom_update(&self->trees[idx], old, tree_put, change);

		tree_put(&old, change);
		if (old.zeroed > 0 && !atom_load(&self->contains_zeroed))
			atom_store(&self->contains_zeroed, true);
		if (old.kind == TREE_HUGE.id &&
		    old.free >> LLFREE_CHILD_ORDER > old.zeroed &&
		    !atom_load(&self->contains_huge))
			atom_store(&self->contains_huge, true);
	}
	return res;
}

llfree_result_t llfree_install(llfree_t *self, uint64_t frame)
{
	return lower_install(&self->lower, frame);
}

void llfree_print_debug(const llfree_t *self,
			void (*writer)(void *, const char *), void *arg)
{
	assert(self != NULL);

	size_t movable_trees = 0;
	size_t free_trees = 0;
	for (size_t i = 0; i < self->trees_len; i++) {
		tree_t tree = atom_load(&self->trees[i]);
		if (tree.free == LLFREE_TREE_SIZE)
			free_trees += 1;
		else if (tree.kind == TREE_MOVABLE.id)
			movable_trees += 1;
	}

	char msg[256];

	ll_stats_t stats = llfree_stats(self);
	snprintf(msg, sizeof(msg),
		 "LLC { cores: %" PRIuS ", frames: %" PRIuS "/%" PRIuS
		 ", huge: %" PRIuS "/%" PRIuS "/%" PRIuS ", trees: %" PRIuS
		 "/%" PRIuS " m=%" PRIuS " }\n",
		 llfree_cores(self), stats.free_frames, stats.frames,
		 stats.zeroed_huge, stats.free_huge, stats.huge, free_trees,
		 self->trees_len, movable_trees);
	writer(arg, msg);
}

void llfree_print(const llfree_t *self)
{
	llfree_info_start();
	llfree_info_cont("llfree_t {\n");
	ll_stats_t stats = llfree_stats(self);

	llfree_info_cont("%sframes: %" PRIuS ", huge: %" PRIuS "\n", INDENT(1),
			 stats.frames, stats.huge);

	size_t free_trees = 0;
	for (size_t i = 0; i < self->trees_len; i++) {
		tree_t tree = atom_load(&self->trees[i]);
		if (tree.free == LLFREE_TREE_SIZE)
			free_trees += 1;
	}
	llfree_info_cont("%sfree: { frames: %" PRIuS ", huge: %" PRIuS
			 ", trees: %" PRIuS " }\n",
			 INDENT(1), stats.free_frames, stats.free_huge,
			 free_trees);

	ll_local_print(self->local, 1);

	llfree_info_cont("%strees: %" PRIuS " (%u) {\n", INDENT(1),
			 self->trees_len, LLFREE_TREE_SIZE);

	for (size_t i = 0; i < self->trees_len; i++) {
		tree_t tree = atom_load(&self->trees[i]);
		tree_print(&tree, i, 2);
	}
	llfree_info_cont("%s}\n", INDENT(1));

	llfree_info_cont("}");
	llfree_info_end();
}

#define check(x)                               \
	({                                     \
		if (!(x)) {                    \
			llfree_warn("failed"); \
			assert(false);         \
		}                              \
	})

#define check_m(x, fmt, ...)                                        \
	({                                                          \
		if (!(x)) {                                         \
			llfree_warn("failed: " fmt, ##__VA_ARGS__); \
			assert(false);                              \
		}                                                   \
	})

#define check_equal(fmt, actual, expected)                                 \
	({                                                                 \
		if ((actual) != (expected)) {                              \
			llfree_warn("failed: %" fmt " == %" fmt, (actual), \
				    (expected));                           \
			assert(false);                                     \
		}                                                          \
	})

static void validate_tree(const llfree_t *self, local_result_t res)
{
	assert(res.present);
	assert(tree_from_row(res.start_row) < self->trees_len);
	tree_t tree = atom_load(&self->trees[tree_from_row(res.start_row)]);

	check(tree.reserved);
	check(tree.kind < TREE_KINDS);
	check(tree.kind <= res.tree.kind);
	treeF_t free = tree.free + res.tree.free;
	check(free <= LLFREE_TREE_SIZE);
	check(tree.zeroed + res.tree.zeroed <= LLFREE_TREE_SIZE);
	if (tree.kind == TREE_HUGE.id) {
		check(res.tree.free % (1 << LLFREE_HUGE_ORDER) == 0);
		check(tree.free % (1 << LLFREE_HUGE_ORDER) == 0);
		check(tree.zeroed <= free >> LLFREE_CHILD_ORDER);
	}
	ll_stats_t tree_stats = lower_stats_at(
		&self->lower, frame_from_row(res.start_row), LLFREE_TREE_ORDER);
	check_equal(PRIuS, (size_t)free, tree_stats.free_frames);
}

void llfree_validate(const llfree_t *self)
{
	ll_stats_t stats = lower_stats(&self->lower);
	// llfree free is computed differently
	ll_stats_t fast_stats = llfree_stats(self);
	check_equal(PRIuS, stats.frames, fast_stats.frames);
	check_equal(PRIuS, stats.huge, fast_stats.huge);
	check_equal(PRIuS, stats.free_frames, fast_stats.free_frames);
	check(stats.free_huge >= fast_stats.free_huge); // fast might count less
	check(stats.zeroed_huge >= fast_stats.zeroed_huge);

	for (size_t tree_idx = 0; tree_idx < self->trees_len; tree_idx++) {
		tree_t tree = atom_load(&self->trees[tree_idx]);
		check(tree.free <= LLFREE_TREE_SIZE);
		check(tree.kind < TREE_KINDS);
		if (!tree.reserved) {
			ll_stats_t tree_stats = lower_stats_at(
				&self->lower, frame_from_tree(tree_idx),
				LLFREE_TREE_ORDER);
			ll_stats_t fast_tree_stats =
				llfree_stats_at(self, frame_from_tree(tree_idx),
						LLFREE_TREE_ORDER);
			check_equal(PRIuS, tree_stats.free_frames,
				    (size_t)tree.free);
			check_equal(PRIuS, fast_tree_stats.free_frames,
				    (size_t)tree.free);

			if (tree.kind == TREE_HUGE.id) {
				check(tree.free % (1 << LLFREE_HUGE_ORDER) ==
				      0);
				check(tree.zeroed <= tree.free >>
				      LLFREE_CHILD_ORDER);
				check_equal(PRIuS, fast_tree_stats.free_huge,
					    (size_t)tree.free >>
						    LLFREE_HUGE_ORDER);
				check_equal(PRIuS, tree_stats.free_huge,
					    (size_t)tree.free >>
						    LLFREE_HUGE_ORDER);

				check_equal(PRIuS, tree_stats.zeroed_huge,
					    (size_t)tree.zeroed);
				check_equal(PRIuS, fast_tree_stats.zeroed_huge,
					    (size_t)tree.zeroed);
			} else {
				check(tree.zeroed == 0);
			}
		}
	}

	ll_local_validate(self->local, self, validate_tree);
}
