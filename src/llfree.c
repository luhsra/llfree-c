#include "bitfield.h"
#include "llfree_inner.h"

#include "llfree.h"
#include "child.h"
#include "llfree_platform.h"
#include "tree.h"
#include "local.h"
#include "lower.h"
#include "utils.h"

llflags_t llflags(size_t order, size_t local)
{
	assert(order < (1u << 4) && local < (1u << 12));
	return (llflags_t){ .order = (uint8_t)order, .local = (uint16_t)local };
}
llkind_t llkind(uint8_t id)
{
	assert(id < (1u << LLKIND_BITS));
	return (llkind_t){ .id = id };
}
llkind_desc_t llkind_desc(llkind_t kind, uint8_t count)
{
	assert(count > 0);
	return (llkind_desc_t){ .kind = kind, .count = count };
}

static inline llfree_result_t ll_unused llfree_result_lower(lower_result_t res,
							    llkind_t kind)
{
	return lower_is_ok(res) ? llfree_ok(res.frame, kind) :
				  llfree_err(res.error);
}

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
		llkind_t kind = sum == LLFREE_TREE_SIZE ? LLKIND_HUGE :
							  llkind(0);
		self->trees[tree_idx] = tree_new(false, kind, sum);
	}
}

llfree_meta_size_t llfree_metadata_size(const llkind_desc_t *kinds,
					size_t frames)
{
	size_t tree_len = div_ceil(frames, LLFREE_TREE_SIZE);
	size_t tree_size =
		align_up(sizeof(tree_t) * tree_len, LLFREE_CACHE_SIZE);
	llfree_meta_size_t meta = {
		.llfree = sizeof(llfree_t),
		.trees = tree_size,
		.local = ll_local_size(kinds),
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

llfree_result_t llfree_init(llfree_t *self, const llkind_desc_t *kinds,
			    size_t frames, uint8_t init, llfree_meta_t meta)
{
	assert(self != NULL);
	assert(check_meta(meta, llfree_metadata_size(kinds, frames)));

	if (init >= LLFREE_INIT_MAX) {
		llfree_info("Invalid init mode %d", init);
		return llfree_err(LLFREE_ERR_INIT);
	}
	if (frames < MIN_PAGES || frames > MAX_PAGES) {
		llfree_info("Invalid size %" PRIu64, (uint64_t)frames);
		return llfree_err(LLFREE_ERR_INIT);
	}

	lower_result_t res = lower_init(&self->lower, frames, init, meta.lower);
	if (!lower_is_ok(res)) {
		return llfree_err(res.error);
	}

	self->trees_len = div_ceil(frames, LLFREE_TREE_SIZE);
	self->trees = (_Atomic(tree_t) *)meta.trees;
	self->local = (local_t *)meta.local;
	ll_local_init(self->local, kinds);

	self->contains_huge = init != LLFREE_INIT_ALLOC;

	if (init != LLFREE_INIT_NONE)
		init_trees(self);
	return llfree_ok(0, llkind(0));
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

typedef size_t (*prio_fn)(llflags_t flags, tree_t tree, void *args);

#define SEARCH_BEST 2
__attribute((unused)) static llfree_result_t
search_best(llfree_t *self, llflags_t flags, uint64_t start, uint64_t offset,
	    uint64_t len, prio_fn tree_prio, search_fn callback, void *args)
{
	assert(self != NULL && callback != NULL);

	struct best {
		size_t prio; // present if > 0
		uint64_t idx;
	};
	struct best best[SEARCH_BEST] = { 0 };
	for (size_t i = 0; i < SEARCH_BEST; ++i)
		best[i].prio = 0;

	for (int64_t i = (int64_t)offset; i < (int64_t)len; ++i) {
		// Search alternating left and right from start
		int64_t off = i % 2 == 0 ? i / 2 : -((i + 1) / 2);
		int64_t base = (int64_t)(start + self->trees_len);
		uint64_t idx = (uint64_t)(base + off) % self->trees_len;

		tree_t tree = atom_load(&self->trees[idx]);
		size_t prio = (tree_prio)(flags, tree, args);
		if (prio == 0)
			continue;

		// Using highest prio directly
		if (prio >= 100) {
			llfree_result_t res = callback(self, idx, args);

			if (res.error != LLFREE_ERR_MEMORY)
				return res;
			continue;
		}

		size_t pos = 0;
		for (; pos < SEARCH_BEST; ++pos) {
			//  Only replace if better -> prefer trees near the start
			if (prio > best[pos].prio)
				break;
		}
		if (pos < SEARCH_BEST) {
			for (size_t j = pos; j < SEARCH_BEST - 1; ++j) {
				best[j + 1] = best[j];
			}
			best[pos].prio = prio;
			best[pos].idx = idx;
		}
	}
	// Try best candidates
	for (size_t i = 0; i < SEARCH_BEST; ++i) {
		if (best[i].prio == 0)
			break;
		llfree_result_t res = callback(self, best[i].idx, args);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;
	}

	return llfree_err(LLFREE_ERR_MEMORY);
}

/// Swap out the currently reserved tree for a new one and writes back the free
/// counter to the previously reserved global tree.
///
/// This checks first if the reserving flag has the expected value
/// returning false if not.
static void swap_reserved(llfree_t *self, size_t local, size_t new_idx,
			  treeF_t free)
{
	local_result_t res = ll_local_swap(self->local, local, new_idx, free);
	assert(res.success);
	if (res.present) {
		tree_t tree;
		atom_update(&self->trees[tree_from_row(res.start_row)], tree,
			    tree_unreserve, res.tree.free,
			    llkind(res.tree.kind));
	}
}

typedef struct reserve_args {
	size_t local;
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

	llkind_t kind = ll_local_kind(self->local, rargs->local);

	tree_t old;
	if (!atom_update(&self->trees[idx], old, tree_reserve, min, max, kind))
		return llfree_err(LLFREE_ERR_MEMORY);
	assert(!old.reserved && old.free >= min);

	lower_result_t res = lower_get(&self->lower, frame_from_tree(idx),
				       ll_none(), flags.order);

	if (lower_is_ok(res)) {
		swap_reserved(self, rargs->local, idx,
			      old.free - (1 << flags.order));
	} else {
		llfree_debug("reserve failed l=%zu idx=%zu kind=%s",
			     rargs->local, idx, tree_kind_name(kind));
		// undo reservation
		tree_t tree;
		atom_update(&self->trees[idx], tree, tree_unreserve, old.free,
			    llkind(old.kind));
	}
	return llfree_result_lower(res, kind);
}

static inline size_t tree_prio(llflags_t flags, tree_t tree, void *args)
{
	(void)args;
	if (tree.free == 0)
		return 0; // entirely allocated
	if (tree.free == LLFREE_TREE_SIZE)
		return 0; // entirely free (dont fragment free trees!)
	if (tree.free < (1u << flags.order))
		return 0; // not enough free frames

	// perfect match -> highest prio
	if (flags.order >= LLFREE_HUGE_ORDER)
		return 100;

	if (tree.free >= LLFREE_TREE_SIZE / 2)
		return 100;
	if (tree.free <= 8)
		return 1;

	return 2; // fallback
}

/// Reserves a new tree and allocates from it.
///
/// The search for a new tree aims to be both fast and avoid fragmentation.
static llfree_result_t reserve_and_get(llfree_t *self, size_t local,
				       llflags_t flags, uint64_t start)
{
#if 1
	assert(start < self->trees_len);

	llfree_result_t res;
	const size_t cl_trees = LLFREE_CACHE_SIZE / sizeof(tree_t);
	// 16 starting points seam to work well
	size_t near = LL_MAX(self->trees_len / 16, cl_trees / 4);
	// Align start to twice the near distance, so we have a gap between starting points
	start = align_down(start, next_pow2(2 * near));

	reserve_args_t args = { .local = local,
				.flags = flags,
				.free = { 0, 0 } };

	llfree_debug("reserve l=%zu o=%d kind=%s (z=%d)", local, flags.order,
		     tree_kind_name(tree_kind_flags(flags)), flags.zeroed);

	if (flags.order < LLFREE_HUGE_ORDER) {
		// Here we do not want to fragment free trees!
		args.free = (p_range_t){ 0, LLFREE_TREE_SIZE - 1 };

		res = search_best(self, flags, start, 1, 2 * near, tree_prio,
				  reserve_tree_and_get, &args);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;

		res = search(self, start, 1, self->trees_len,
			     reserve_tree_and_get, &args);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;
	}

	// Any tree
	args.free = (p_range_t){ 0, LLFREE_TREE_SIZE };
	res = search(self, start, 0, self->trees_len, reserve_tree_and_get,
		     &args);
#else
	const size_t cl_trees = 32;

	llfree_result_t res;

	assert(start < self->trees_len);
	start = align_down(start, cl_trees);

	size_t near = (self->trees_len / ll_local_len(self->local)) / 4;
	near = LL_MIN(LL_MAX(near, cl_trees / 4), cl_trees * 2);

	reserve_args_t args = { .local = local,
				.flags = flags,
				.free = { 0, 0 } };
	// Over half filled trees
	args.free = (p_range_t){ LLFREE_TREE_SIZE / 16, LLFREE_TREE_SIZE / 2 };
	res = search(self, start, 1, near, reserve_tree_and_get, &args);
	if (res.error != LLFREE_ERR_MEMORY)
		return res;

	// Partially filled tree
	args.free = (p_range_t){ LLFREE_TREE_SIZE / 64,
				 LLFREE_TREE_SIZE - LLFREE_TREE_SIZE / 16 };
	res = search(self, start, 1, 2 * near, reserve_tree_and_get, &args);
	if (res.error != LLFREE_ERR_MEMORY)
		return res;

	// Not free tree
	args.free = (p_range_t){ 0, LLFREE_TREE_SIZE - 1 };
	res = search(self, start, 1, near, reserve_tree_and_get, &args);
	if (res.error != LLFREE_ERR_MEMORY)
		return res;

	// Any tree
	args.free = (p_range_t){ 0, LLFREE_TREE_SIZE };
	res = search(self, start, 0, self->trees_len, reserve_tree_and_get,
		     &args);
#endif
	return res;
}

/// Synchronizes the free_counter of given local with the global counter.
static bool ll_unused sync_with_global(llfree_t *self, llflags_t flags,
				       size_t tree_idx, size_t free)
{
	assert(self != NULL);

	size_t min = (1 << flags.order) - free;

	tree_t old_tree;
	if (!atom_update(&self->trees[tree_idx], old_tree, tree_steal_counter,
			 min))
		return false;
	assert(old_tree.reserved && old_tree.free >= min);

	if (!ll_local_put(self->local, flags.local, tree_idx, old_tree.free)) {
		llfree_warn("sync local failed c=%zu idx=%zu",
			    (size_t)flags.local, tree_idx);
		// Revert the steal
		atom_update(&self->trees[tree_idx], old_tree, tree_put,
			    old_tree.free);
		return false;
	}
	llfree_debug("sync success c=%zu kind=%s free=%zu", (size_t)flags.local,
		     (size_t)old_tree.kind, (size_t)old_tree.free);
	return true;
}

static llfree_result_t get_from_local(llfree_t *self, llflags_t flags,
				      ll_optional_t frame, local_result_t *old)
{
	// Try decrementing the local counter
	ll_optional_t tree_idx = frame.present ?
					 ll_some(tree_from_frame(frame.value)) :
					 ll_none();
	*old = ll_local_get(self->local, flags.local, tree_idx,
			    1 << flags.order);
	if (old->success) {
		lower_result_t res = lower_get(&self->lower,
					       frame_from_row(old->start_row),
					       frame, flags.order);
		if (lower_is_ok(res)) {
			size_t start_row = row_from_frame(res.frame);
			// Update local start index
			if (old->start_row != start_row) {
				ll_local_set_start(self->local, flags.local,
						   start_row);
			}
			llkind_t kind = ll_local_kind(self->local, flags.local);
			return llfree_ok(res.frame, kind);
		}

		// Undo decrement (inc global tree)
		tree_t tree;
		size_t tree_idx = tree_from_row(old->start_row);
		atom_update(&self->trees[tree_idx], tree, tree_put,
			    1 << flags.order);

		return llfree_err(res.error);
	}

	// Try synchronizing with global counter
	if (old->present &&
	    sync_with_global(self, flags, tree_from_row(old->start_row),
			     old->tree.free)) {
		// Success -> Retry allocation
		return llfree_err(LLFREE_ERR_RETRY);
	}

	return llfree_err(LLFREE_ERR_MEMORY);
}

/// Search and steal from any reserved tree
static llfree_result_t steal_from_reserved(llfree_t *self, size_t core,
					   llflags_t flags, ll_optional_t frame)
{
	local_result_t lres;

	ll_optional_t tree_idx = frame.present ?
					 ll_some(tree_from_frame(frame.value)) :
					 ll_none();
	lres = ll_local_steal(self->local, core, tree_idx, 1 << flags.order);
	if (lres.success) {
		lower_result_t res = lower_get(&self->lower,
					       frame_from_row(lres.start_row),
					       frame, flags.order);
		if (res.error == LLFREE_ERR_MEMORY) {
			// Undo steal
			tree_t tree;
			atom_update(&self->trees[tree_from_row(lres.start_row)],
				    tree, tree_put, 1 << flags.order);
		} else {
			return llfree_result_lower(res, llkind(lres.tree.kind));
		}
	}

	lres = ll_local_steal_downgrade(self->local, core, tree_idx,
					1 << flags.order);
	if (lres.success) {
		// unreserve
		if (lres.present) {
			tree_t old;
			atom_update(&self->trees[tree_from_row(lres.start_row)],
				    old, tree_unreserve, lres.tree.free,
				    llkind(lres.tree.kind));
		}

		lower_result_t res = lower_get(&self->lower,
					       frame_from_row(lres.start_row),
					       frame, flags.order);
		if (res.error == LLFREE_ERR_MEMORY) {
			// Undo steal
			tree_t tree;
			atom_update(&self->trees[tree_from_row(lres.start_row)],
				    tree, tree_put, 1 << flags.order);
		} else {
			return llfree_result_lower(res, llkind(lres.tree.kind));
		}

		return llfree_err(LLFREE_ERR_MEMORY);
	}

	return llfree_err(LLFREE_ERR_RETRY);
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
	llkind_t kind = ll_local_kind(self->local, flags.local);
	if (!atom_update(&self->trees[idx], old, tree_get_demote,
			 1 << flags.order, kind))
		return llfree_err(LLFREE_ERR_MEMORY);
	// Might be lower due to stealing
	kind.id = LL_MIN(kind.id, old.kind);

	assert(old.free >= (1 << flags.order));

	lower_result_t res = lower_get(&self->lower, frame_from_tree(idx),
				       ll_none(), flags.order);

	if (!lower_is_ok(res)) {
		// undo reservation
		tree_t expected = old;
		tree_get(&expected, 1 << flags.order, kind);
		// first try to also reset the tree kind if nothing has changed
		if (!atom_cmp_exchange(&self->trees[idx], &expected, old)) {
			// keep tree kind as other cpu might have already used it
			atom_update(&self->trees[idx], expected, tree_put,
				    1 << flags.order);
		}
	}
	return llfree_result_lower(res, kind);
}

static llfree_result_t get_any_global(llfree_t *self, size_t start,
					llflags_t flags)
{
	alloc_global_args_t args = { .flags = flags };
	return search(self, start, 0, self->trees_len, alloc_global_tree,
		      &args);
}

llfree_result_t llfree_get(llfree_t *self, ll_optional_t frame, llflags_t flags)
{
	assert(self != NULL);
	assert(flags.order <= LLFREE_MAX_ORDER);

	size_t core = flags.local % ll_local_len(self->local);
	uint64_t start = self->trees_len / ll_local_len(self->local) * core;
	llfree_result_t res;

	for (size_t i = 0; i < RETRIES; i++) {
		local_result_t old;
		res = get_from_local(self, flags, frame, &old);
		if (res.error == LLFREE_ERR_RETRY)
			continue;
		if (res.error != LLFREE_ERR_MEMORY)
			return res;

		if (old.present)
			start = tree_from_row(old.start_row);

		if (!frame.present) {
			// reserve new tree
			res = reserve_and_get(self, core, flags, start);
			if (res.error != LLFREE_ERR_MEMORY)
				return res;
		}
		// Few trees -> high probability that they are shared
		if (ll_local_len(self->local) <= 4) {
			// If reservation fails, there might be another concurrent update -> retry
			ll_optional_t tree_idx =
				frame.present ?
					ll_some(tree_from_frame(frame.value)) :
					ll_none();
			for (size_t j = 0; j < RETRIES; j++) {
				if (ll_local_can_get(self->local, flags.local,
						     tree_idx,
						     1 << flags.order))
					break;
				spin_wait();
			}
		}
	}
	// Global search for the specific frame
	if (frame.present) {
		llkind_t kind = ll_local_kind(self->local, core);
		tree_t old;
		if (atom_update(&self->trees[tree_from_frame(frame.value)], old,
				tree_get_demote, 1 << flags.order, kind)) {
			lower_result_t lres = lower_get(&self->lower, frame.value, frame,
					flags.order);
			if (lres.error != LLFREE_ERR_MEMORY)
				return llfree_result_lower(lres, kind);

			// Undo reservation and continue
			atom_update(&self->trees[tree_from_frame(frame.value)],
				    old, tree_put, 1 << flags.order);
		}
	}

	for (size_t i = 0; i < RETRIES; i++) {
		// take a huge frame from the other reserved trees
		res = steal_from_reserved(self, core, flags, frame);
		if (res.error == LLFREE_ERR_RETRY)
			continue;
		if (res.error != LLFREE_ERR_MEMORY)
			return res;
	}

	// just take from any tree
	if (!frame.present)
		res = get_any_global(self, start, flags);
	return res;
}

llfree_result_t llfree_put(llfree_t *self, uint64_t frame, llflags_t flags)
{
	assert(self != NULL);
	assert(flags.order <= LLFREE_MAX_ORDER);
	assert(frame < self->lower.frames);

	llkind_t kind = ll_local_kind(self->local, flags.local);
	lower_result_t res = lower_put(&self->lower, frame, flags.order);
	if (!lower_is_ok(res)) {
		llfree_info("lower err %" PRIu64, (uint64_t)res.error);
		return llfree_result_lower(res, kind);
	}

	// Frame is successfully freed in lower allocator

	size_t tree_idx = tree_from_frame(frame);

	// Update free-reserve heuristic
	bool reserve = LLFREE_ENABLE_FREE_RESERVE &&
		       ll_local_free_inc(self->local, flags.local, tree_idx);

	tree_t old;
	treeF_t frames = 1 << flags.order;

	// Update reservation if present
	if (ll_local_put(self->local, flags.local, tree_idx, frames)) {
		reserve = false;
	} else {
		// Update global tree or reserve
		atom_update(&self->trees[tree_idx], old, tree_put_or_reserve,
			    frames, kind, &reserve, TREE_LOWER_LIM);
	}

	if (flags.order >= LLFREE_CHILD_ORDER) {
		if (!atom_load(&self->contains_zeroed))
			atom_store(&self->contains_zeroed, true);
		if (!atom_load(&self->contains_huge))
			atom_store(&self->contains_huge, true);
	}

	// Free-reserve heuristic:
	// Reserve trees where a lot of frees happen, assuming locality
	if (reserve) {
		tree_t new = old;
		bool success = tree_put(&new, frames);
		assert(success);
		// fully reserve the new tree
		new.reserved = true;
		swap_reserved(self, flags.local, tree_idx, new.free);
	}
	return llfree_err(LLFREE_ERR_OK);
}

llfree_result_t llfree_drain(llfree_t *self, size_t local)
{
	ll_local_drain(self->local, local);
	return llfree_err(LLFREE_ERR_OK);
}

size_t llfree_frames(const llfree_t *self)
{
	assert(self != NULL);
	return self->lower.frames;
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
		if (t.kind == LLKIND_HUGE.id) {
			stats.free_huge += t.free >> LLFREE_HUGE_ORDER;
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
		if (tree.kind == LLKIND_HUGE.id) {
			stats.free_huge = tree.free >> LLFREE_HUGE_ORDER;
		}
		if (tree.reserved) {
			ll_stats_t local_stats =
				ll_local_stats_at(self->local, frame);
			stats.free_frames += local_stats.free_frames;
			stats.free_huge += local_stats.free_huge;
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

#if 0
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
	}

	llfree_result_t res = lower_reclaim(&self->lower,
					    idx << LLFREE_TREE_ORDER, alloc,
					    not_reclaimed, not_zeroed);

	if (!llfree_is_ok(res)) {
		// If we changed the tree, undo this transaction
		if (alloc || (not_zeroed && old.kind == LLKIND_HUGE.id)) {
			bool changed = atom_update(&self->trees[idx], old,
						   tree_undo_reclaim,
						   not_zeroed, alloc);
			assert(!alloc || changed);
		}
	}
	return res;
}

llfree_result_t llfree_reclaim(llfree_t *self, size_t local, bool alloc,
			       bool not_reclaimed, bool not_zeroed)
{
	size_t start_idx = ll_local_reclaimed(self->local, local);

	// Search and allocate
	llfree_result_t ret = llfree_err(LLFREE_ERR_MEMORY);
	// First try to reclaim non-zeroed frames
	struct try_reclaim_args args = { .alloc = alloc,
					 .not_reclaimed = not_reclaimed,
					 .not_zeroed = true };

	// Search for non-zeroed first
	ret = search(self, start_idx, 0, self->trees_len, try_reclaim, &args);

	// Fallback to any frame
	if (!not_zeroed && !llfree_is_ok(ret) &&
	    ret.error == LLFREE_ERR_MEMORY) {
		args.not_zeroed = false;
		ret = search(self, start_idx, 0, self->trees_len, try_reclaim,
			     &args);
	}

	// Update the local reclaim index
	if (llfree_is_ok(ret) && start_idx != tree_from_frame(ret.frame)) {
		llfree_debug("Set reclaim_idx %" PRIuS " -> %" PRIuS, start_idx,
			     tree_from_frame(ret.frame));
		ll_local_set_reclaimed(self->local, local,
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
		treeF_t frames = 1 << LLFREE_HUGE_ORDER;
		atom_update(&self->trees[idx], old, tree_put, frames);

		tree_put(&old, frames);
		if (old.kind == LLKIND_ZERO.id &&
		    !atom_load(&self->contains_zeroed))
			atom_store(&self->contains_zeroed, true);
		if (old.kind == LLKIND_HUGE.id &&
		    !atom_load(&self->contains_huge))
			atom_store(&self->contains_huge, true);
	}
	return res;
}

llfree_result_t llfree_install(llfree_t *self, uint64_t frame)
{
	return lower_install(&self->lower, frame);
}
#endif

void llfree_print_debug(const llfree_t *self,
			void (*writer)(void *, const char *), void *arg)
{
	assert(self != NULL);

	size_t used_trees = 0;
	size_t free_trees = 0;
	for (size_t i = 0; i < self->trees_len; i++) {
		tree_t tree = atom_load(&self->trees[i]);
		if (tree.free == LLFREE_TREE_SIZE)
			free_trees += 1;
		else if (tree.free < LLFREE_TREE_SIZE)
			used_trees += 1;
	}

	char msg[256];

	ll_stats_t stats = llfree_stats(self);
	snprintf(msg, sizeof(msg),
		 "LLC { local: %" PRIuS ", frames: %" PRIuS "/%" PRIuS
		 ", huge: %" PRIuS "/%" PRIuS ", trees: %" PRIuS "/%" PRIuS
		 " used=%" PRIuS " }\n",
		 ll_local_len(self->local), stats.free_frames, stats.frames,
		 stats.free_huge, stats.huge, free_trees, self->trees_len,
		 used_trees);
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
	check(tree.kind >= res.tree.kind);
	treeF_t free = tree.free + res.tree.free;
	check(free <= LLFREE_TREE_SIZE);
	if (tree.kind == LLKIND_HUGE.id) {
		check(res.tree.free % (1 << LLFREE_HUGE_ORDER) == 0);
		check(tree.free % (1 << LLFREE_HUGE_ORDER) == 0);
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

	for (size_t tree_idx = 0; tree_idx < self->trees_len; tree_idx++) {
		tree_t tree = atom_load(&self->trees[tree_idx]);
		check(tree.free <= LLFREE_TREE_SIZE);
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

			if (tree.kind == LLKIND_HUGE.id) {
				check(tree.free % (1 << LLFREE_HUGE_ORDER) ==
				      0);
				check_equal(PRIuS, fast_tree_stats.free_huge,
					    (size_t)tree.free >>
						    LLFREE_HUGE_ORDER);
				check_equal(PRIuS, tree_stats.free_huge,
					    (size_t)tree.free >>
						    LLFREE_HUGE_ORDER);
			}
		}
	}

	ll_local_validate(self->local, self, validate_tree);
}
