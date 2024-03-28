#include "llfree_inner.h"

#include "llfree.h"
#include "child.h"
#include "tree.h"
#include "local.h"
#include "lower.h"

/// Returns the local data of given core
static inline local_t *get_local(llfree_t *self, size_t core)
{
	return &self->local[core % self->cores];
}

/// Initializes the Tree array by reading the child counters
static void init_trees(llfree_t *self)
{
	assert(self != NULL);

	for (size_t tree_idx = 0; tree_idx < self->trees_len; ++tree_idx) {
		uint16_t sum = 0;
		for (size_t child_idx = 0; child_idx < LLFREE_TREE_CHILDREN;
		     ++child_idx) {
			if (child_idx >=
			    div_ceil(self->lower.frames, LLFREE_CHILD_SIZE))
				break;
			child_t child =
				atom_load(&self->lower.children[tree_idx]
						   .entries[child_idx]);
			sum += child.free;
		}
		self->trees[tree_idx] = tree_new(sum, false, false);
	}
}

llfree_meta_size_t llfree_metadata_size(size_t cores, size_t frames)
{
	size_t tree_len = div_ceil(frames, LLFREE_TREE_SIZE);
	size_t tree_size =
		align_up(sizeof(tree_t) * tree_len, LLFREE_CACHE_SIZE);
	size_t local_len = LL_MIN(cores, tree_len);
	size_t local_size =
		align_up(sizeof(local_t), LLFREE_CACHE_SIZE) * local_len;
	llfree_meta_size_t meta = {
		.primary = lower_metadata_size(frames),
		.secondary = local_size + tree_size,
	};
	return meta;
}

llfree_result_t llfree_init(llfree_t *self, size_t cores, size_t frames,
			    uint8_t init, uint8_t *primary, uint8_t *secondary)
{
	assert(self != NULL);
	assert(primary != NULL && secondary != NULL);
	assert((size_t)primary % LLFREE_CACHE_SIZE == 0 &&
	       (size_t)secondary % LLFREE_CACHE_SIZE == 0);

	llfree_meta_size_t _unused meta = llfree_metadata_size(cores, frames);
	// no overlap!
	assert(primary + meta.primary <= secondary ||
	       secondary + meta.secondary <= primary);

	if (init != LLFREE_INIT_FREE && init != LLFREE_INIT_ALLOC &&
	    init != LLFREE_INIT_RECOVER && init != LLFREE_INIT_RECOVER_CRASH) {
		llfree_warn("Invalid init mode %d", init);
		return llfree_result(LLFREE_ERR_INIT);
	}
	if (frames < MIN_PAGES || frames > MAX_PAGES) {
		llfree_warn("Invalid size %" PRIu64, (uint64_t)frames);
		return llfree_result(LLFREE_ERR_INIT);
	}

	llfree_result_t res = lower_init(&self->lower, frames, init, primary);
	if (!llfree_ok(res)) {
		return res;
	}

	// check if more cores than trees -> if not shared locale data
	self->trees_len = div_ceil(frames, LLFREE_TREE_SIZE);
	self->cores = LL_MIN(cores, self->trees_len);
	size_t local_size =
		align_up(sizeof(local_t), LLFREE_CACHE_SIZE) * self->cores;

	self->local = (local_t *)secondary;
	self->trees = (_Atomic(tree_t) *)(secondary + local_size);
	assert((size_t)(self->local + self->cores) <= (size_t)self->trees);
	assert((size_t)self->trees <= (size_t)(secondary + meta.secondary));

	// init local data do default 0
	for (size_t local_idx = 0; local_idx < self->cores; ++local_idx) {
		ll_local_init(&self->local[local_idx]);
	}

	init_trees(self);
	return llfree_result(LLFREE_ERR_OK);
}

llfree_meta_t llfree_metadata(llfree_t *self)
{
	assert(self != NULL);
	llfree_meta_t meta = {
		.primary = lower_metadata(&self->lower),
		.secondary = (uint8_t *)self->local,
	};
	return meta;
}

/// Swap out the currently reserved tree for a new one and writes back the free
/// counter to the previously reserved global tree.
///
/// This checks first if the reserving flag has the expected value
/// returning false if not.
static void swap_reserved(llfree_t *self, local_t *local, reserved_t new,
			  bool movable)
{
	reserved_t old = *ll_local_reserved(local, movable);
	*ll_local_reserved(local, movable) = new;

	if (old.present) {
		size_t tree_idx = tree_from_row(old.start_row);
		size_t free = old.free;
		tree_t tree;
		atom_update(&self->trees[tree_idx], tree, tree_writeback, free,
			    movable);
	}
}

/// Reserves a new tree and allocates with it.
/// Only if the allocation succeeds, the tree is fully reserved.
/// Otherwise the reservation is reverted.
static llfree_result_t reserve_tree_and_get(llfree_t *self, local_t *local,
					    size_t idx, llflags_t flags,
					    p_range_t free)
{
	assert(idx < self->trees_len);
	free.min = LL_MAX(free.min, 1 << flags.order);

	size_t span = LL_MIN(LLFREE_TREE_SIZE,
			     self->lower.frames - idx * LLFREE_TREE_SIZE);

	tree_t old;
	if (!atom_update(&self->trees[idx], old, tree_reserve, free.min,
			 free.max, span, flags.movable))
		return llfree_result(LLFREE_ERR_MEMORY);
	assert(!old.reserved && old.free >= (1 << flags.order));

	llfree_result_t res =
		lower_get(&self->lower, pfn_from_tree(idx), flags);

	if (llfree_ok(res)) {
		// write back llfree_result
		reserved_t new = { .free = old.free - (1 << flags.order),
				   .start_row = row_from_pfn(res.val),
				   .present = true };
		swap_reserved(self, local, new, flags.movable);
	} else {
		// undo reservation
		tree_t tree;
		atom_update(&self->trees[idx], tree, tree_writeback, old.free,
			    flags.movable);
	}
	return res;
}

static bool tree_better(tree_t a, reserved_t b, size_t order)
{
	const size_t min_pages = LL_MAX(2 << order, 4);
	if (a.free < min_pages)
		return false;
	if (!b.present)
		return true;
	return a.free < b.free;
}

/// Searches globally for the best tree to minimize fragmentation.
static _unused llfree_result_t search_best(llfree_t *self, local_t *local,
					   size_t base_idx, llflags_t flags)
{
	size_t total_free = 0;
	const size_t cl_trees = LLFREE_CACHE_SIZE / sizeof(tree_t);
	size_t near = LL_MIN(LL_MAX((self->trees_len / self->cores / 4),
				    cl_trees / 4),
			     cl_trees * 2);

	reserved_t best[2] = { { .present = false }, { .present = false } };

	for (int64_t i = 1; i < self->trees_len; ++i) {
		// Search alternating left and right from base_idx
		int64_t toggle = i % 2 == 0 ? i / 2 : -(i + 1) / 2;
		size_t idx =
			(self->trees_len + base_idx + toggle) % self->trees_len;
		tree_t tree = atom_load(&self->trees[idx]);
		total_free += tree.free;

		if (tree_better(tree, best[0], flags.order)) {
			best[1] = best[0];
			best[0] = (reserved_t){
				.free = tree.free,
				.start_row = row_from_tree(idx),
				.present = true,
			};
		} else if (tree_better(tree, best[1], flags.order)) {
			best[1] = (reserved_t){
				.free = tree.free,
				.start_row = row_from_tree(idx),
				.present = true,
			};
		}

		if (!(i % near == 0 || i == self->trees_len - 1))
			continue;

		size_t average_free = total_free / i;
		const size_t max_free = LL_MIN(
			LLFREE_TREE_SIZE,
			average_free +
				LLFREE_TREE_SIZE / (self->trees_len / (i + 1)));

		for (size_t i = 0; i < 2; i++) {
			if (best[i].present && best[i].free <= max_free) {
				p_range_t range = { 1 << flags.order,
						    max_free };
				llfree_result_t res = reserve_tree_and_get(
					self, local,
					tree_from_row(best[i].start_row), flags,
					range);
				if (res.val != LLFREE_ERR_MEMORY)
					return res;
			}
		}

		// Allocation failed, reset best
		best[0].present = false;
		best[1].present = false;
	}

	return llfree_result(LLFREE_ERR_MEMORY);
}

/// Searches the tree array starting at base_idx for a tree with a
/// free counter in the provided range, reserves it, and allocates from it.
static llfree_result_t search(llfree_t *self, local_t *local, uint64_t base_idx,
			      llflags_t flags, p_range_t free, uint64_t offset,
			      uint64_t len)
{
	int64_t start = (int64_t)(base_idx + self->trees_len);
	for (int64_t i = (int64_t)offset; i < (int64_t)len; ++i) {
		// Search alternating left and right from base_idx
		int64_t off = i % 2 == 0 ? i / 2 : -(i + 1) / 2;
		uint64_t idx = (start + off) % self->trees_len;
		llfree_result_t res =
			reserve_tree_and_get(self, local, idx, flags, free);
		if (res.val != LLFREE_ERR_MEMORY)
			return res;
	}
	return llfree_result(LLFREE_ERR_MEMORY);
}

/// Reserves a new tree and allocates from it.
///
/// The search for a new tree aims to be both fast and avoid fragmentation.
static llfree_result_t reserve_and_get(llfree_t *self, uint64_t core,
				       llflags_t flags)
{
	local_t *local = get_local(self, core);
	reserved_t old = *ll_local_reserved(local, flags.movable);
	llfree_result_t res;

	uint64_t start_idx =
		old.present ?
			tree_from_row(old.start_row) :
			(self->trees_len / self->cores * (core % self->cores));
	assert(start_idx < self->trees_len);

	const size_t cl_trees = LLFREE_CACHE_SIZE / sizeof(tree_t);
	size_t near = LL_MIN(LL_MAX((self->trees_len / self->cores / 4),
				    cl_trees / 4),
			     cl_trees * 2);

	uint64_t base_idx = align_down(start_idx, cl_trees);

	// Over half filled trees
	p_range_t half = { LL_MAX(LLFREE_TREE_SIZE / 16, 2 << flags.order),
			   LLFREE_TREE_SIZE / 2 };
	res = search(self, local, base_idx, flags, half, 1, near);
	if (res.val != LLFREE_ERR_MEMORY)
		return res;

	// Partially filled tree
	p_range_t partial = { LL_MAX(LLFREE_TREE_SIZE / 64, 2 << flags.order),
			      LLFREE_TREE_SIZE - LLFREE_TREE_SIZE / 16 };
	res = search(self, local, base_idx, flags, partial, 1, 2 * near);
	if (res.val != LLFREE_ERR_MEMORY)
		return res;

	// Not free tree
	p_range_t notfree = { 0, LLFREE_TREE_SIZE - 4 };
	res = search(self, local, base_idx, flags, notfree, 1, 4 * near);
	if (res.val != LLFREE_ERR_MEMORY)
		return res;

	// Any tree
	p_range_t any = { 0, LLFREE_TREE_SIZE };
	res = search(self, local, base_idx, flags, any, 0, self->trees_len);
	if (res.val != LLFREE_ERR_MEMORY)
		return res;

	// Drain other cores for a tree
	for (uint64_t i = 1; i < self->cores; ++i) {
		res = llfree_drain(self, (core + i) % self->cores);
	}
	// Repeat search
	res = search(self, local, base_idx, flags, any, 0, self->trees_len);

	return res;
}

/// Synchronizes the free_counter of given local with the global counter.
static bool sync_with_global(llfree_t *self, local_t *local, llflags_t flags)
{
	assert(self != NULL && local != NULL);
	reserved_t *reserved = ll_local_reserved(local, flags.movable);
	assert(reserved->present && reserved->free < (1 << flags.order));

	size_t tree_idx = tree_from_row(reserved->start_row);

	tree_t old_tree;
	if (!atom_update(&self->trees[tree_idx], old_tree, tree_steal_counter,
			 (1 << flags.order) - reserved->free))
		return false;

	reserved->free += old_tree.free;
	return true;
}

static llfree_result_t get_inner(llfree_t *self, size_t core, llflags_t flags)
{
	assert(self != NULL);
	assert(flags.order <= LLFREE_MAX_ORDER);

	local_t *local = get_local(self, core);
	ll_local_lock(local);

	reserved_t *reserved = ll_local_reserved(local, flags.movable);

	if (reserved->present) {
		// Try decrementing the local counter
		if (reserved->free >= (1 << flags.order)) {
			llfree_result_t res = lower_get(
				&self->lower, pfn_from_row(reserved->start_row),
				flags);
			if (llfree_ok(res)) {
				reserved->free -= (1 << flags.order);
				reserved->start_row = row_from_pfn(res.val);
			} else if (res.val == LLFREE_ERR_MEMORY) {
				// Current tree is fragmented!
				res = reserve_and_get(self, core, flags);
			}
			ll_local_unlock(local);
			return res;
		}
		// Try sync with global counter
		if (sync_with_global(self, local, flags)) {
			// Success -> Retry allocation
			ll_local_unlock(local);
			return llfree_result(LLFREE_ERR_RETRY);
		}
	}

	llfree_result_t res = reserve_and_get(self, core, flags);
	ll_local_unlock(local);
	return res;
}

llfree_result_t llfree_get(llfree_t *self, size_t core, llflags_t flags)
{
	assert(self != NULL);
	assert(flags.order <= LLFREE_MAX_ORDER);

	// Ignore movability on small zones
	if (flags.movable && self->trees_len <= LL_MAX(self->cores, 4))
		flags.movable = false;

	for (size_t i = 0; i < RETRIES; i++) {
		llfree_result_t res = get_inner(self, core, flags);

		if (llfree_ok(res) || res.val != LLFREE_ERR_RETRY)
			return res;
	}

	llfree_warn("Exceeding retries");
	return llfree_result(LLFREE_ERR_MEMORY);
}

llfree_result_t llfree_put(llfree_t *self, size_t core, uint64_t frame,
			   llflags_t flags)
{
	assert(self != NULL);
	assert(flags.order <= LLFREE_MAX_ORDER);
	assert(frame < self->lower.frames);
	assert(!flags.movable);

	llfree_result_t res = lower_put(&self->lower, frame, flags);
	if (!llfree_ok(res)) {
		llfree_warn("lower err %" PRId64, res.val);
		return res;
	}

	// Frame is successfully freed in lower allocator
	local_t *local = get_local(self, core);
	ll_local_lock(local);

	size_t tree_idx = tree_from_pfn(frame);

	// Update free-reserve heuristic
	bool reserve = ll_local_free_inc(local, tree_idx);

	// Increment local or otherwise global counter
	if (local->fixed.present &&
	    tree_from_row(local->fixed.start_row) == tree_idx) {
		local->fixed.free += 1 << flags.order;
		assert(local->fixed.free <= LLFREE_TREE_SIZE);
		ll_local_unlock(local);
		return llfree_result(LLFREE_ERR_OK);
	}
	if (local->movable.present &&
	    tree_from_row(local->movable.start_row) == tree_idx) {
		local->movable.free += 1 << flags.order;
		assert(local->fixed.free <= LLFREE_TREE_SIZE);
		ll_local_unlock(local);
		return llfree_result(LLFREE_ERR_OK);
	}

	tree_t old_t;
	size_t span = LL_MIN(LLFREE_TREE_SIZE,
			     self->lower.frames - tree_idx * LLFREE_TREE_SIZE);
	atom_update(&self->trees[tree_idx], old_t, tree_inc_or_reserve,
		    1 << flags.order, &reserve, TREE_LOWER_LIM,
		    LLFREE_TREE_SIZE - (2 << LLFREE_HUGE_ORDER), span);

	// Free-reserve heuristic:
	// Reserve trees where a lot of frees happen, assuming locality
	if (reserve) {
		// fully reserve the new tree
		size_t free = old_t.free + (1 << flags.order);
		assert(!old_t.reserved && free <= LLFREE_TREE_SIZE);

		reserved_t new = { .free = free,
				   .start_row = row_from_tree(tree_idx),
				   .present = true };
		swap_reserved(self, local, new, old_t.movable);
	}
	ll_local_unlock(local);
	return llfree_result(LLFREE_ERR_OK);
}

llfree_result_t llfree_drain(llfree_t *self, size_t core)
{
	local_t *local = get_local(self, core);
	if (ll_local_try_lock(local)) {
		reserved_t none = {
			.free = 0,
			.start_row = 0,
			.present = false,
		};
		swap_reserved(self, local, none, false);
		swap_reserved(self, local, none, true);
		ll_local_unlock(local);
	}
	return llfree_result(LLFREE_ERR_OK);
}

size_t llfree_cores(llfree_t *self)
{
	assert(self != NULL);
	return self->cores;
}

size_t llfree_frames(llfree_t *self)
{
	assert(self != NULL);
	return self->lower.frames;
}

size_t llfree_free_frames(llfree_t *self)
{
	assert(self != NULL);
	uint64_t free = 0;
	// Local counters
	for (size_t core = 0; core < self->cores; core++) {
		reserved_t rf = get_local(self, core)->fixed;
		reserved_t rm = get_local(self, core)->movable;
		free += rf.free + rm.free;
	}
	// Global counters
	for (size_t i = 0; i < self->trees_len; i++) {
		tree_t t = atom_load(&self->trees[i]);
		free += t.free;
	}
	return free;
}

size_t llfree_free_huge(llfree_t *self)
{
	assert(self != NULL);
	// Count in the lower allocator
	return lower_free_huge(&self->lower);
}

bool llfree_is_free(llfree_t *self, uint64_t frame, size_t order)
{
	assert(self != NULL);
	return lower_is_free(&self->lower, frame, order);
}

size_t llfree_free_at(llfree_t *self, uint64_t frame, size_t order)
{
	assert(self != NULL);
	if (order == 0) // is_free is sufficient for order 0
		return lower_is_free(&self->lower, frame, 0);
	if (order == LLFREE_HUGE_ORDER)
		return lower_free_at_huge(&self->lower, frame);
	if (order == LLFREE_TREE_ORDER) {
		size_t tree_idx = frame >> LLFREE_TREE_ORDER;
		assert(tree_idx < self->trees_len);
		tree_t tree = atom_load(&self->trees[tree_idx]);
		return tree.free;
	}
	return 0;
}

void llfree_print_debug(llfree_t *self, void (*writer)(void *, char *),
			void *arg)
{
	assert(self != NULL);

	size_t free_trees = 0;
	for (size_t i = 0; i < self->trees_len; i++) {
		tree_t tree = atom_load(&self->trees[i]);
		if (tree.free == LLFREE_TREE_SIZE)
			free_trees += 1;
	}

	char msg[256];
	snprintf(msg, 256,
		 "LLC { cores: %" PRIuS ", frames: %" PRIuS "/%" PRIuS
		 ", huge: %" PRIuS "/%" PRIuS ", trees: %" PRIuS "/%" PRIuS
		 " }\n",
		 self->cores, llfree_free_frames(self), self->lower.frames,
		 lower_free_huge(&self->lower),
		 div_ceil(self->lower.frames, LLFREE_CHILD_SIZE), free_trees,
		 self->trees_len);
	writer(arg, msg);
}

void llfree_print(llfree_t *self)
{
	llfree_info_start();
	llfree_info_cont("llfree_t {\n");
	llfree_info_cont("    frames: %" PRIuS "\n", self->lower.frames);
	llfree_info_cont("    trees: %" PRIuS " (%u) {\n", self->trees_len,
			 LLFREE_TREE_SIZE);
	size_t _unused free_huge = llfree_free_huge(self);
	size_t _unused free_trees = 0;
	for (size_t i = 0; i < self->trees_len; i++) {
		tree_t tree = atom_load(&self->trees[i]);
		llfree_info_cont("        %3" PRIuS ": free: %" PRIuS
				 ", reserved: %d\n",
				 i, (size_t)tree.free, tree.reserved);
		if (tree.free == LLFREE_TREE_SIZE)
			free_trees += 1;
	}
	llfree_info_cont("    }\n");

	llfree_info_cont("    free: { frames: %" PRIuS ", huge: %" PRIuS
			 ", trees: %" PRIuS " }\n",
			 llfree_free_frames(self), free_huge, free_trees);

	for (size_t i = 0; i < self->cores; i++) {
		reserved_t _unused rf = self->local[i].fixed;
		reserved_t _unused rm = self->local[i].movable;
		llfree_info_cont("    local %" PRIuS " (%d): ", i,
				 atom_load(&self->local[i].lock));
		llfree_info_cont("f={ present: %d, free: %" PRIu64
				 ", start: %" PRIu64 "}, ",
				 rf.present, (uint64_t)rf.free,
				 tree_from_row(rf.start_row));
		llfree_info_cont("m={ present: %d, free: %" PRIu64
				 ", start: %" PRIu64 "}\n",
				 rm.present, (uint64_t)rm.free,
				 tree_from_row(rm.start_row));
	}
	llfree_info_cont("}");
	llfree_info_end();
}
