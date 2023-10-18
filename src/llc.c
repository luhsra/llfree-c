#include "llc.h"
#include "bitfield.h"
#include "child.h"
#include "local.h"
#include "lower.h"
#include "utils.h"
#include <assert.h>

/// magic number to determine on recover if a allocator was previously
/// initialized
#define MAGIC 0xC0FFEE

struct meta {
	/// Marker to find the parsistent state
	uint32_t magic;
	/// If it has crashed
	bool crashed;
};

/// returns pointer to local data of given core
#define get_local(_self, _core) ({ &_self->local[_core % _self->cores]; })

/**
 * @brief Increases the Free counter of given Tree
 * It will increase the local counter if it has the right tree reserved
 * otherwise the global counter is increased
 */
static size_t inc_tree_counter(llc_t *self, local_t *const local,
			       size_t tree_idx, size_t order)
{
	reserved_t old;
	if (!atom_update(&local->reserved, old, local_inc_counter, tree_idx,
			 1 << order)) {
		// given tree was not the local tree -> increase global counter
		tree_t old;
		bool _unused success = atom_update(&self->trees[tree_idx], old,
						   tree_inc, order);
		assert(success);
	}
	return tree_from_row(old.start_row);
}

static result_t swap_reserved(llc_t *self, local_t *local, reserved_t new,
			      bool expect_reserving)
{
	reserved_t old;
	if (!atom_update(&local->reserved, old, local_swap_reserved, new,
			 expect_reserving)) {
		return result(ERR_RETRY);
	}

	if (old.present) {
		size_t tree_idx = tree_from_row(old.start_row);
		tree_t t;
		if (!atom_update(&self->trees[tree_idx], t, tree_writeback,
				 old.free)) {
			warn("Failed writeback");
			return result(ERR_CORRUPTION);
		}
	}
	return result(ERR_OK);
}

/**
 * @brief synchronizes the free_counter of given local with the global counter.
 *
 * @param self pointer to upper allocator
 * @param local pointer to local struct
 * @return True if frames were added to the local tree, false otherwise
 */
static bool sync_with_global(llc_t *self, local_t *const local, size_t order)
{
	assert(self != NULL);
	assert(local != NULL);

	// get Index of reserved Tree
	reserved_t reserved = atom_load(&local->reserved);
	size_t tree_idx = tree_from_row(reserved.start_row);

	tree_t old_tree;
	bool _unused ret = atom_update(&self->trees[tree_idx], old_tree,
				       tree_steal_counter);
	assert(ret);

	size_t free = old_tree.free;
	if (free < (1 << order))
		return false;

	if (atom_update(&self->local->reserved, reserved, local_inc_counter,
			tree_idx, free)) {
		return true;
	}
	// undo changes
	ret = atom_update(&self->trees[tree_idx], old_tree, tree_writeback,
			  free);
	assert(ret);
	return false;
}

/**
 * @brief Initializes the Tree array by reading the child counters
 * works non Atomically!
 * @param self pointer to upper allocator
 */
static void init_trees(llc_t *self)
{
	assert(self != NULL);

	for (size_t tree_idx = 0; tree_idx < self->trees_len; ++tree_idx) {
		uint16_t sum = 0;
		for (size_t child_idx = CHILDS_PER_TREE * tree_idx;
		     child_idx < CHILDS_PER_TREE * (tree_idx + 1);
		     ++child_idx) {
			if (child_idx >= self->lower.childs_len)
				break;
			child_t child =
				atom_load(&self->lower.childs[child_idx]);
			sum += child.free;
		}
		self->trees[tree_idx] = tree_new(sum, false);
	}
}

/**
 * @brief Set the preferred tree in local data to given tree and writes back
 * the previous tree;
 *
 * @param self pointer to Upper Allocator
 * @param local pointer to the local data of the reserving core
 * @param tree_idx index of the Tree to be reserved
 * @return ERR_OK on success
 *         ERR_ADDRESS if tree could not be reserved
 */
static result_t reserve_tree(llc_t *self, local_t *local, uint64_t tree_idx,
			     range_t free, bool expect_reserving)
{
	assert(self != NULL);
	assert(local != NULL);
	assert(tree_idx < self->trees_len);

	tree_t old_tree;
	if (!atom_update(&self->trees[tree_idx], old_tree, tree_reserve,
			 free.min, free.max))
		return result(ERR_MEMORY);

	reserved_t new = { old_tree.free, row_from_tree(tree_idx), true,
			   false };
	return swap_reserved(self, local, new, expect_reserving);
}

static result_t alloc_frame(llc_t *self, local_t *local, uint64_t order)
{
	reserved_t old;
	if (!atom_update(&local->reserved, old, local_dec_counter, 1 << order))
		return result(ERR_MEMORY);

	uint64_t start_pfn = pfn_from_row(old.start_row);

	result_t res = lower_get(&self->lower, start_pfn, order);
	if (result_ok(res)) {
		// save pfn only if necessary
		if ((1 << order) < ATOMICSIZE &&
		    old.start_row != row_from_pfn(res.val)) {
			atom_update(&local->reserved, old, local_reserve_index,
				    tree_from_pfn(res.val));
		}
	} else {
		// undo decrement
		inc_tree_counter(self, local, tree_from_pfn(start_pfn), order);
	}
	return res;
}

static result_t reserve_and_get_tree(llc_t *self, local_t *local, size_t idx,
				     size_t order, range_t free)
{
	tree_t old_tree;
	if (!atom_update(&self->trees[idx], old_tree, tree_reserve, free.min,
			 free.max))
		return result(ERR_MEMORY);
	assert(old_tree.free >= (1 << order));

	result_t res = lower_get(&self->lower, pfn_from_tree(idx), order);

	if (result_ok(res)) {
		// write back result
		reserved_t new = { old_tree.free - (1 << order),
				   row_from_pfn(res.val), true, false };
		if (!result_ok(swap_reserved(self, local, new, true))) {
			warn("Invalid reserve state");
			return result(ERR_CORRUPTION);
		}
	} else {
		// undo reservation
		if (!atom_update(&self->trees[idx], old_tree, tree_writeback,
				 old_tree.free)) {
			warn("Failed undo");
			return result(ERR_CORRUPTION);
		}
	}
	return res;
}

/// searches the whole tree array starting at base_idx for a tree with
/// saturation status and reserves it for local
static result_t search_global(llc_t *self, local_t *local, uint64_t base_idx,
			      uint64_t order, range_t free)
{
	// search outside of current cacheline for a partial tree
	for (size_t i = 1; i <= self->trees_len; ++i) {
		const int64_t toggle = i & 1 ? i / 2 : -i / 2;
		const uint64_t idx = (base_idx + toggle) % self->trees_len;
		result_t res =
			reserve_and_get_tree(self, local, idx, order, free);
		if (res.val != ERR_MEMORY)
			return res;
	}
	return result(ERR_MEMORY);
}

static result_t reserve_and_get(llc_t *self, local_t *local, uint64_t core,
				uint64_t order)
{
	reserved_t reserved = atom_load(&local->reserved);
	uint64_t start_idx;
	if (reserved.present) {
		start_idx = tree_from_row(reserved.start_row);
	} else {
		start_idx =
			self->trees_len / self->cores * (core % self->cores);
	}
	assert(start_idx < self->trees_len);

	const uint64_t offset = start_idx % CHILDS_PER_TREE;
	const uint64_t base_idx = start_idx - offset;
	uint64_t vicinity = self->trees_len / self->cores / 4;
	if (vicinity == 0)
		vicinity = 1;
	else if (vicinity > CHILDS_PER_TREE)
		vicinity = CHILDS_PER_TREE;

	// search inside of current cacheline for a partial tree
	for (size_t i = 1; i <= vicinity; ++i) {
		const int64_t toggle = i & 1 ? i / 2 : -i / 2;
		const uint64_t idx = (base_idx + toggle) % self->trees_len;
		result_t res =
			reserve_and_get_tree(self, local, idx, order,
					     (range_t){ 1 << order, TREESIZE });
		if (res.val != ERR_MEMORY)
			return res;
	}

	// search globally for a frame
	result_t res;
	res = search_global(self, local, base_idx, order, TREE_PARTIAL);
	if (res.val != ERR_MEMORY)
		return res;

	res = search_global(self, local, base_idx, order, TREE_FREE);
	if (res.val != ERR_MEMORY)
		return res;

	// drain other cores for a tree
	uint64_t local_idx = core % self->cores;
	for (uint64_t i = 1; i < self->cores; ++i) {
		const uint64_t idx = (local_idx + i) % self->cores;
		llc_drain(self, idx);
	}

	// search whole tree for a tree with enough free frames
	res = search_global(self, local, base_idx, order,
			    (range_t){ 1 << order, TREESIZE });

	// clear mark reserving
	if (!result_ok(res)) {
		reserved_t old;
		bool _unused res = atom_update(&local->reserved, old,
					       local_unmark_reserving);
		assert(res);
	}
	return res;
}

/// Initializes the allocator for the given memory region, returning 0 on
/// success or a negative error code
result_t llc_init(llc_t *self, size_t cores, uint64_t offset, size_t len,
		  uint8_t init, uint8_t free_all)
{
	assert(self != NULL);
	if (init != VOLATILE && init != OVERWRITE && init != RECOVER) {
		warn("Invalid init mode %d", init);
		return result(ERR_INITIALIZATION);
	}

	// check if given memory is enough
	if (len < MIN_PAGES || len > MAX_PAGES) {
		warn("Invalid size %lu", len);
		return result(ERR_INITIALIZATION);
	}
	// check on unexpected Memory alignment
	if ((offset * PAGESIZE) % (1 << MAX_ORDER) != 0) {
		warn("Invalid alignment");
		return result(ERR_INITIALIZATION);
	}

	if (init == VOLATILE) {
		self->meta = NULL;
	} else {
		len -= 1;
		uint64_t last_page = (offset + len) * PAGESIZE;
		self->meta = (struct meta *)last_page;
	}

	lower_init(&self->lower, offset, len, init);
	if (init == RECOVER) {
		if (self->meta->magic != MAGIC) {
			warn("Invalid magic");
			return result(ERR_INITIALIZATION);
		}
		if (self->meta->crashed)
			lower_recover(&self->lower);
	} else {
		lower_clear(&self->lower, free_all);
	}

	self->trees_len = div_ceil(self->lower.childs_len, CHILDS_PER_TREE);
	self->trees =
		llc_ext_alloc(CACHESIZE, sizeof(child_t) * self->trees_len);
	assert(self->trees != NULL);
	if (self->trees == NULL)
		return result(ERR_INITIALIZATION);

	// check if more cores than trees -> if not shared locale data
	size_t local_len = MIN(cores, self->trees_len);
	self->cores = local_len;
	self->local = llc_ext_alloc(CACHESIZE, sizeof(local_t) * local_len);

	assert(self->local != NULL);
	if (self->trees == NULL)
		return result(ERR_INITIALIZATION);

	// init local data do default 0
	for (size_t local_idx = 0; local_idx < self->cores; ++local_idx) {
		local_init(&self->local[local_idx]);
	}

	init_trees(self);

	if (init != VOLATILE) {
		self->meta->magic = MAGIC;
		self->meta->crashed = true;
	}
	return result(ERR_OK);
}

/// Allocates a frame and returns its address, or a negative error code
result_t llc_get(llc_t *self, size_t core, size_t order)
{
	assert(self != NULL);
	assert(order <= MAX_ORDER);

	local_t *local = get_local(self, core);

	result_t res = alloc_frame(self, local, order);
	if (!result_ok(res)) {
		reserved_t reserved = atom_load(&local->reserved);
		if (reserved.present && reserved.free < (1 << order) &&
		    sync_with_global(self, local, order)) {
			res = alloc_frame(self, local, order);
		}
	}

	for (size_t i = 0; i < 4 && !result_ok(res); i++) {
		reserved_t old;
		if (atom_update(&local->reserved, old, local_mark_reserving)) {
			res = reserve_and_get(self, local, core, order);
		} else {
			local_wait_reserving(local);
			res = alloc_frame(self, local, order);
		}
		if (res.val != ERR_RETRY)
			break;
	}

	if (result_ok(res)) {
		return result(res.val + self->lower.offset);
	}
	warn("OOM!!!");

	return res;
}

/// Frees a frame, returning 0 on success or a negative error code
result_t llc_put(llc_t *self, size_t core, uint64_t frame, size_t order)
{
	assert(self != NULL);
	assert(order <= MAX_ORDER);

	if (frame < self->lower.offset ||
	    frame >= self->lower.offset + self->lower.frames)
		return result(ERR_ADDRESS);
	frame -= self->lower.offset;
	assert(frame < self->lower.frames);

	result_t res = lower_put(&self->lower, frame, order);
	if (!result_ok(res)) {
		warn("lower err %ld", res.val);
		return res;
	}

	// frame is successfully freed in lower allocator
	local_t *local = get_local(self, core);
	size_t tree_idx = tree_from_pfn(frame);

	size_t resv_tree = inc_tree_counter(self, local, tree_idx, order);

	// set last reserved in local
	last_free_t old;
	atom_update(&local->last_free, old, local_inc_last_free, tree_idx);

	if (resv_tree != tree_idx && old.counter >= 3) {
		// this tree was the target of multiple consecutive frees
		// -> reserve this tree if it is not completely allocated
		result_t ret = reserve_tree(
			self, local, tree_idx,
			(range_t){ TREE_LOWER_LIM, TREE_UPPER_LIM }, false);

		if (!result_ok(ret) && ret.val != ERR_MEMORY &&
		    ret.val != ERR_RETRY) {
			return ret;
		}
	}
	return result(ERR_OK);
}

result_t llc_drain(llc_t *self, size_t core)
{
	local_t *local = get_local(self, core);

	reserved_t none = { 0, 0, false, false };
	result_t res = swap_reserved(self, local, none, false);
	if (result_ok(res) || res.val == ERR_RETRY)
		return result(ERR_OK);
	return res;
}

/// Returns the total number of frames the allocator can allocate
uint64_t llc_frames(llc_t *self)
{
	assert(self != NULL);
	return self->lower.frames;
}

/// Returns number of currently free frames
uint64_t llc_free_frames(llc_t *self)
{
	assert(self != NULL);
	uint64_t free = 0;
	for (size_t i = 0; i < self->trees_len; i++) {
		tree_t t = atom_load(&self->trees[i]);
		free += t.free;
	}
	for (size_t core = 0; core < self->cores; core++) {
		reserved_t r = atom_load(&get_local(self, core)->reserved);
		free += r.free;
	}
	return free;
}

uint8_t llc_is_free(llc_t *self, uint64_t frame_adr, size_t order)
{
	assert(self != NULL);
	return lower_is_free(&self->lower, frame_adr, order);
}

void llc_drop(llc_t *self)
{
	assert(self != NULL);

	if (self->meta != NULL)
		self->meta->crashed = false;

	// if initialized
	if (self->local != NULL) {
		lower_drop(&self->lower);
		llc_ext_free(CACHESIZE, self->trees_len * sizeof(tree_t),
			     self->trees);
		llc_ext_free(CACHESIZE, self->cores * sizeof(local_t),
			     self->local);
	}
}

void llc_for_each_huge(llc_t *self, void *context,
		       void f(void *, uint64_t, uint64_t))
{
	assert(self != NULL);
	// llc_print(self);
	lower_for_each_child(&self->lower, context, f);
}

/// Prints the allocators state for debugging
void llc_debug(llc_t *self, void (*writer)(void *, char *), void *arg)
{
	assert(self != NULL);

	writer(arg, "\nLLC stats:\n");
	char *msg = llc_ext_alloc(1, 200 * sizeof(char));
	snprintf(msg, 200, "frames:\t%7lu\tfree: %7lu\tallocated: %7lu\n",
		 self->lower.frames, llc_free_frames(self),
		 self->lower.frames - llc_free_frames(self));
	writer(arg, msg);

	snprintf(msg, 200, "HPs:\t%7lu\tfree: %7lu\tallocated: %7lu\n",
		 self->lower.childs_len, lower_free_huge(&self->lower),
		 self->lower.childs_len - lower_free_huge(&self->lower));
	writer(arg, msg);
}

/**
 * @brief Debug-function prints information over trees and local Data
 *
 * @param self
 */
void llc_print(llc_t *self)
{
	printf("-----------------------------------------------\n"
	       "UPPER ALLOCATOR\nTrees:\t%lu\nCores:\t%lu\n allocated: %lu, free: %lu, "
	       "all: %lu\n",
	       self->trees_len, self->cores,
	       llc_frames(self) - llc_free_frames(self), llc_free_frames(self),
	       llc_frames(self));

	printf("\nTrees:\n-----------------------------------------------\n");
	if (self->trees_len > 20)
		printf("There are over 20 Trees. Print will only contain first and last "
		       "10\n\n");

	printf("Nr:\t\t");
	for (size_t i = 0; i < self->trees_len; ++i) {
		if (i < 10 || i >= self->trees_len - 10)
			printf("%lu\t", i);
	}
	printf("\nreserved:\t");
	for (size_t i = 0; i < self->trees_len; ++i) {
		if (i < 10 || i >= self->trees_len - 10) {
			tree_t tree = atom_load(&self->trees[i]);
			printf("%d\t", tree.flag);
		}
	}
	printf("\nfree:\t\t");
	for (size_t i = 0; i < self->trees_len; ++i) {
		if (i < 10 || i >= self->trees_len - 10) {
			tree_t tree = atom_load(&self->trees[i]);
			printf("%d\t", tree.free);
		}
	}
	printf("\n");

	printf("-----------------------------------------------\n"
	       "Local Data:\n"
	       "-----------------------------------------------\n"
	       "Core\t\t");
	for (size_t i = 0; i < self->cores; ++i) {
		printf("%lu\t", i);
	}
	printf("\nhas_tree:\t");
	for (size_t i = 0; i < self->cores; ++i) {
		reserved_t reserved = atom_load(&get_local(self, i)->reserved);
		printf("%d\t", reserved.present);
	}
	printf("\nTreeIDX:\t");
	for (size_t i = 0; i < self->cores; ++i) {
		reserved_t reserved = atom_load(&get_local(self, i)->reserved);
		printf("%lu\t", tree_from_row(reserved.start_row));
	}
	printf("\nFreeFrames:\t");
	for (size_t i = 0; i < self->cores; ++i) {
		reserved_t reserved = atom_load(&get_local(self, i)->reserved);
		printf("%u\t", reserved.free);
	}

	lower_print(&self->lower);

	printf("\n-----------------------------------------------\n");
	fflush(stdout);
}
