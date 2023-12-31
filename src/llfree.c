#include "llfree_inner.h"

#include "llfree.h"
#include "child.h"
#include "tree.h"
#include "local.h"
#include "lower.h"
#include "utils.h"

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
		for (size_t child_idx = LLFREE_TREE_CHILDREN * tree_idx;
		     child_idx < LLFREE_TREE_CHILDREN * (tree_idx + 1);
		     ++child_idx) {
			if (child_idx >= self->lower.childs_len)
				break;
			child_t child =
				atom_load(&self->lower.children[child_idx]);
			sum += child.free;
		}
		self->trees[tree_idx] = tree_new(sum, false);
	}
}

llfree_meta_size_t llfree_metadata_size(size_t cores, size_t frames)
{
	size_t tree_len = div_ceil(frames, LLFREE_TREE_SIZE);
	size_t tree_size =
		align_up(sizeof(tree_t) * tree_len, LLFREE_CACHE_SIZE);
	size_t local_len = MIN(cores, tree_len);
	size_t local_size =
		align_up(sizeof(local_t) * local_len, LLFREE_CACHE_SIZE);
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
	self->cores = MIN(cores, self->trees_len);
	size_t local_size =
		align_up(sizeof(local_t) * self->cores, LLFREE_CACHE_SIZE);

	self->local = (local_t *)secondary;
	self->trees = (_Atomic(tree_t) *)(secondary + local_size);

	// init local data do default 0
	for (size_t local_idx = 0; local_idx < self->cores; ++local_idx) {
		local_init(&self->local[local_idx]);
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
/// returning LLFREE_ERR_RETRY if not.
static llfree_result_t swap_reserved(llfree_t *self, local_t *local,
				     reserved_t new, bool expect_reserving)
{
	reserved_t old;
	if (!atom_update(&local->reserved, old, reserved_swap, new,
			 expect_reserving)) {
		llfree_info("swap resv failed");
		return llfree_result(LLFREE_ERR_RETRY);
	}

	if (old.present) {
		size_t tree_idx = tree_from_row(old.start_row);
		tree_t tree;
		if (!atom_update(&self->trees[tree_idx], tree, tree_writeback,
				 old.free)) {
			uint64_t new_tree = tree_from_row(new.start_row);
			llfree_warn("Failed writeback %" PRIuS " (next %" PRIu64
				    ")",
				    tree_idx, new_tree);
			return llfree_result(LLFREE_ERR_CORRUPT);
		}
		tree = atom_load(&self->trees[tree_idx]);
	}

	return llfree_result(LLFREE_ERR_OK);
}

/// Reserves a new tree and allocates with it.
/// Only if the allocation succeeds, the tree is fully reserved.
/// Otherwise the reservation is reverted.
static llfree_result_t reserve_tree_and_get(llfree_t *self, local_t *local,
					    size_t idx, size_t order,
					    p_range_t free)
{
	tree_t old_tree;
	if (!atom_update(&self->trees[idx], old_tree, tree_reserve, free.min,
			 free.max))
		return llfree_result(LLFREE_ERR_MEMORY);
	assert(old_tree.free >= (1 << order));

	llfree_result_t res =
		lower_get(&self->lower, pfn_from_tree(idx), order);

	if (llfree_ok(res)) {
		// write back llfree_result
		reserved_t new = { .free = old_tree.free - (1 << order),
				   .start_row = row_from_pfn(res.val),
				   .present = true,
				   .reserving = false };
		if (!llfree_ok(swap_reserved(self, local, new, true))) {
			llfree_warn("Invalid reserve state");
			return llfree_result(LLFREE_ERR_CORRUPT);
		}
	} else {
		// undo reservation
		size_t free = old_tree.free;
		if (!atom_update(&self->trees[idx], old_tree, tree_writeback,
				 free)) {
			llfree_warn("Failed undo");
			return llfree_result(LLFREE_ERR_CORRUPT);
		}
	}
	return res;
}

/// Searches the whole tree array starting at base_idx for a tree with
/// a free counter in the provided range, reserves it, and allocates from it.
static llfree_result_t search_global(llfree_t *self, local_t *local,
				     uint64_t base_idx, uint64_t order,
				     p_range_t free)
{
	// search outside of current cacheline for a partial tree
	for (int64_t i = 1; i <= (int64_t)self->trees_len; ++i) {
		int64_t toggle = i & 1 ? i / 2 : -i / 2;
		uint64_t idx = (base_idx + toggle) % self->trees_len;
		llfree_result_t res =
			reserve_tree_and_get(self, local, idx, order, free);
		if (res.val != LLFREE_ERR_MEMORY)
			return res;
	}
	return llfree_result(LLFREE_ERR_MEMORY);
}

/// Reserves a new tree and allocates from it.
///
/// The search for a new tree aims to be both fast and avoid fragmentation.
static llfree_result_t reserve_and_get(llfree_t *self, uint64_t core,
				       uint64_t order, reserved_t old)
{
	local_t *local = get_local(self, core);

	uint64_t start_idx;
	if (old.present) {
		start_idx = tree_from_row(old.start_row);
	} else {
		start_idx =
			self->trees_len / self->cores * (core % self->cores);
	}
	assert(start_idx < self->trees_len);

	uint64_t offset = start_idx % LLFREE_TREE_CHILDREN;
	uint64_t base_idx = start_idx - offset;
	uint64_t vicinity = div_ceil(div_ceil(self->trees_len, self->cores), 4);
	if (vicinity > LLFREE_TREE_CHILDREN)
		vicinity = LLFREE_TREE_CHILDREN;

	// search inside of current cacheline for a partial tree
	for (int64_t i = 1; i <= (int64_t)vicinity; ++i) {
		int64_t toggle = i & 1 ? i / 2 : -i / 2;
		uint64_t idx = (base_idx + toggle) % self->trees_len;
		llfree_result_t res = reserve_tree_and_get(
			self, local, idx, order,
			(p_range_t){ 1 << order, LLFREE_TREE_SIZE });
		if (res.val != LLFREE_ERR_MEMORY)
			return res;
	}

	// search globally for a frame
	llfree_result_t res;
	res = search_global(self, local, base_idx, order, TREE_PARTIAL);
	if (res.val != LLFREE_ERR_MEMORY)
		return res;
	res = search_global(self, local, base_idx, order, TREE_FREE);
	if (res.val != LLFREE_ERR_MEMORY)
		return res;

	// drain other cores for a tree
	for (uint64_t i = 1; i < self->cores; ++i) {
		res = llfree_drain(self, (core + i) % self->cores);
	}

	// search whole tree for a tree with enough free frames
	res = search_global(self, local, base_idx, order,
			    (p_range_t){ 1 << order, LLFREE_TREE_SIZE });

	// clear reserving
	if (!llfree_ok(res)) {
		reserved_t old;
		bool _unused res = atom_update(&local->reserved, old,
					       reserved_set_reserving, false);
		assert(res);
	}
	return res;
}

static llfree_result_t reserve_or_wait(llfree_t *self, size_t core,
				       size_t order)
{
	local_t *local = get_local(self, core);
	reserved_t reserved;
	if (atom_update(&local->reserved, reserved, reserved_set_reserving,
			true)) {
		return reserve_and_get(self, core, order, reserved);
	}
	llfree_info("spin wait");
	while (({
		reserved_t r = atom_load(&local->reserved);
		r.reserving;
	})) {
		spin_wait();
	}
	return llfree_result(LLFREE_ERR_RETRY);
}

/// Synchronizes the free_counter of given local with the global counter.
static bool sync_with_global(llfree_t *self, local_t *local, size_t order,
			     reserved_t reserved)
{
	assert(self != NULL);
	assert(local != NULL);

	assert(reserved.present);
	assert(!reserved.reserving);
	assert(reserved.free < (1 << order));

	// get Index of reserved Tree
	size_t tree_idx = tree_from_row(reserved.start_row);

	tree_t old_tree;
	if (!atom_update(&self->trees[tree_idx], old_tree, tree_steal_counter,
			 (1 << order) - reserved.free))
		return false;

	reserved_t desired = reserved;
	desired.free = reserved.free + old_tree.free;
	if (atom_cmp_exchange(&local->reserved, &reserved, desired))
		return true;

	llfree_info("undo sync global");
	// undo changes
	bool _unused ret = atom_update(&self->trees[tree_idx], old_tree,
				       tree_inc, desired.free);
	assert(ret);

	return false;
}

static llfree_result_t get_inner(llfree_t *self, size_t core, size_t order)
{
	assert(self != NULL);
	assert(order <= LLFREE_MAX_ORDER);

	local_t *local = get_local(self, core);

	// Update the upper counter first
	reserved_t reserved;
	if (!atom_update(&local->reserved, reserved, reserved_dec,
			 1 << order)) {
		// If the local counter is large enough we do not have to reserve a new tree
		// Just update the local counter and reuse the current tree
		if (reserved.present && !reserved.reserving &&
		    reserved.free < (1 << order) &&
		    sync_with_global(self, local, order, reserved)) {
			return llfree_result(
				LLFREE_ERR_RETRY); // Success -> Retry allocation
		}
		// The local tree is full -> reserve a new one
		return reserve_or_wait(self, core, order);
	}

	uint64_t start = pfn_from_row(reserved.start_row);

	llfree_result_t res = lower_get(&self->lower, start, order);
	if (llfree_ok(res)) {
		// save pfn only if necessary
		if (order <= LLFREE_ATOMIC_ORDER &&
		    reserved.start_row != row_from_pfn(res.val)) {
			atom_update(&local->reserved, reserved,
				    reserved_set_start, row_from_pfn(res.val));
		}
		return res;
	}
	if (res.val == LLFREE_ERR_MEMORY) {
		// Current tree is fragmented!
		// Increment global to prevent race condition with concurrent reservation
		tree_t old;
		if (atom_update(&self->trees[tree_from_pfn(start)], old,
				tree_inc, 1 << order)) {
			return reserve_or_wait(self, core, order);
		}

		llfree_warn("undo failed");
		return llfree_result(LLFREE_ERR_CORRUPT);
	}
	return res;
}

llfree_result_t llfree_get(llfree_t *self, size_t core, size_t order)
{
	assert(self != NULL);
	assert(order <= LLFREE_MAX_ORDER);

	for (size_t i = 0; i < RETRIES; i++) {
		llfree_result_t res = get_inner(self, core, order);

		if (llfree_ok(res) || res.val != LLFREE_ERR_RETRY)
			return res;
	}

	llfree_warn("Exceeding retries");
	return llfree_result(LLFREE_ERR_MEMORY);
}

llfree_result_t llfree_put(llfree_t *self, size_t core, uint64_t frame,
			   size_t order)
{
	assert(self != NULL);
	assert(order <= LLFREE_MAX_ORDER);
	assert(frame < self->lower.frames);

	llfree_result_t res = lower_put(&self->lower, frame, order);
	if (!llfree_ok(res)) {
		llfree_warn("lower err %" PRId64, res.val);
		return res;
	}

	// frame is successfully freed in lower allocator
	local_t *local = get_local(self, core);
	size_t tree_idx = tree_from_pfn(frame);

	// increment local or global counters
	reserved_t old;
	if (!atom_update(&local->reserved, old, reserved_inc, tree_idx,
			 1 << order)) {
		// given tree was not the local tree -> increase global counter
		tree_t old;
		bool _unused success = atom_update(&self->trees[tree_idx], old,
						   tree_inc, 1 << order);
		assert(success);
	}

	// set last reserved in local
	last_free_t last_free;
	atom_update(&local->last_free, last_free, last_free_inc, tree_idx);

	// free-reserve heuristic:
	// reserve another tree where a lot of frees happen,
	// assuming that the next frees also target this tree
	if (tree_from_row(old.start_row) != tree_idx &&
	    last_free.tree_idx == tree_idx && last_free.counter >= 3) {
		tree_t old_tree;
		if (atom_update(&self->trees[tree_idx], old_tree, tree_reserve,
				TREE_LOWER_LIM, TREE_UPPER_LIM)) {
			reserved_t new = { .free = old_tree.free,
					   .start_row = row_from_tree(tree_idx),
					   .present = true,
					   .reserving = false };
			llfree_result_t ret =
				swap_reserved(self, local, new, false);

			if (!llfree_ok(ret)) {
				if (ret.val != LLFREE_ERR_RETRY)
					return ret;

				size_t free = old_tree.free;
				if (!atom_update(&self->trees[tree_idx],
						 old_tree, tree_writeback,
						 free)) {
					llfree_warn("Undo failed!");
					return llfree_result(
						LLFREE_ERR_CORRUPT);
				}
			}
		}
	}
	return llfree_result(LLFREE_ERR_OK);
}

llfree_result_t llfree_drain(llfree_t *self, size_t core)
{
	local_t *local = get_local(self, core);

	reserved_t none = {
		.free = 0, .start_row = 0, .present = false, .reserving = false
	};
	llfree_result_t res = swap_reserved(self, local, none, false);
	if (llfree_ok(res) || res.val == LLFREE_ERR_RETRY)
		return llfree_result(LLFREE_ERR_OK);
	return res;
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
	for (size_t core = 0; core < self->cores; core++) {
		reserved_t r = atom_load(&get_local(self, core)->reserved);
		free += r.free;
	}
	for (size_t i = 0; i < self->trees_len; i++) {
		tree_t t = atom_load(&self->trees[i]);
		free += t.free;
	}
	return free;
}

size_t llfree_free_huge(llfree_t *self)
{
	assert(self != NULL);
	return lower_free_huge(&self->lower);
}

bool llfree_is_free(llfree_t *self, uint64_t frame, size_t order)
{
	assert(self != NULL);
	return lower_is_free(&self->lower, frame, order);
}

void llfree_for_each_huge(llfree_t *self, void *context,
			  void f(void *, uint64_t, size_t))
{
	assert(self != NULL);
	lower_for_each_child(&self->lower, context, f);
}

void llfree_print_debug(llfree_t *self, void (*writer)(void *, char *),
			void *arg)
{
	assert(self != NULL);

	char msg[200];
	snprintf(msg, 200,
		 "LLC { frames: %" PRIuS "/%" PRIuS ", huge: %" PRIuS "/%" PRIuS
		 " }",
		 llfree_free_frames(self), self->lower.frames,
		 lower_free_huge(&self->lower), self->lower.childs_len);
	writer(arg, msg);
}

#ifdef STD
void llfree_print(llfree_t *self)
{
	printf("llfree_t {\n");
	printf("    frames: %" PRIuS "\n", self->lower.frames);
	printf("    trees: %" PRIuS " (%u) {\n", self->trees_len,
	       LLFREE_TREE_SIZE);
	size_t free_huge = llfree_free_huge(self);
	size_t free_trees = 0;
	for (size_t i = 0; i < self->trees_len; i++) {
		tree_t tree = atom_load(&self->trees[i]);
		printf("        %3ju: free: %5u, reserved: %d\n", i, tree.free,
		       tree.reserved);
		if (tree.free == LLFREE_TREE_SIZE)
			free_trees += 1;
	}
	printf("    }\n");

	printf("    free: { frames: %" PRIuS ", huge: %" PRIuS
	       ", trees: %" PRIuS " }\n",
	       llfree_free_frames(self), free_huge, free_trees);

	for (size_t i = 0; i < self->cores; i++) {
		reserved_t reserved = atom_load(&self->local[i].reserved);
		printf("    local %" PRIuS
		       ": { present: %d, free: %u, start: %" PRIu64
		       ", reserving: %d }\n",
		       i, reserved.present, reserved.free,
		       tree_from_row(reserved.start_row), reserved.reserving);
	}
	printf("}\n");
}
#endif
