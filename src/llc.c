#include "llc_inner.h"

#include "llc.h"
#include "child.h"
#include "tree.h"
#include "local.h"
#include "lower.h"
#include "utils.h"

/// Magic used the identify a previous allocator state
#define MAGIC 0xC0FFEE

/// Persistent metadata used for recovery
struct meta {
	/// Marker to find the parsistent state
	uint32_t magic;
	/// If it has crashed
	bool crashed;
};

/// Returns the local data of given core
static inline local_t *get_local(llc_t *self, size_t core)
{
	return &self->local[core % self->cores];
}

/// Initializes the Tree array by reading the child counters
static void init_trees(llc_t *self)
{
	assert(self != NULL);

	for (size_t tree_idx = 0; tree_idx < self->trees_len; ++tree_idx) {
		uint16_t sum = 0;
		for (size_t child_idx = TREE_CHILDREN * tree_idx;
		     child_idx < TREE_CHILDREN * (tree_idx + 1); ++child_idx) {
			if (child_idx >= self->lower.childs_len)
				break;
			child_t child =
				atom_load(&self->lower.children[child_idx]);
			sum += child.free;
		}
		self->trees[tree_idx] = tree_new(sum, false);
	}
}

result_t llc_init(llc_t *self, size_t cores, uint64_t offset, size_t len,
		  uint8_t init, uint8_t free_all)
{
	assert(self != NULL);
	if (init != INIT_VOLATILE && init != INIT_OVERWRITE &&
	    init != INIT_RECOVER) {
		warn("Invalid init mode %d", init);
		return result(ERR_INITIALIZATION);
	}
	if (len < MIN_PAGES || len > MAX_PAGES) {
		warn("Invalid size %ju", len);
		return result(ERR_INITIALIZATION);
	}
	if ((offset * FRAME_SIZE) % (1 << MAX_ORDER) != 0) {
		warn("Invalid alignment");
		return result(ERR_INITIALIZATION);
	}

	if (init == INIT_VOLATILE) {
		self->meta = NULL;
	} else {
		len -= 1;
		uint64_t last_page = (offset + len) * FRAME_SIZE;
		self->meta = (struct meta *)last_page;
	}

	lower_init(&self->lower, offset, len, init);

	if (init == INIT_RECOVER) {
		if (self->meta->magic != MAGIC) {
			warn("Invalid magic");
			return result(ERR_INITIALIZATION);
		}
		if (self->meta->crashed)
			lower_recover(&self->lower);
	} else {
		lower_clear(&self->lower, free_all);
	}

	self->trees_len = div_ceil(self->lower.childs_len, TREE_CHILDREN);
	self->trees =
		llc_ext_alloc(CACHE_SIZE, sizeof(child_t) * self->trees_len);
	assert(self->trees != NULL);
	if (self->trees == NULL)
		return result(ERR_INITIALIZATION);

	// check if more cores than trees -> if not shared locale data
	size_t local_len = MIN(cores, self->trees_len);
	self->cores = local_len;
	self->local = llc_ext_alloc(CACHE_SIZE, sizeof(local_t) * local_len);

	assert(self->local != NULL);
	if (self->trees == NULL)
		return result(ERR_INITIALIZATION);

	// init local data do default 0
	for (size_t local_idx = 0; local_idx < self->cores; ++local_idx) {
		local_init(&self->local[local_idx]);
	}

	init_trees(self);

	if (init != INIT_VOLATILE) {
		self->meta->magic = MAGIC;
		self->meta->crashed = true;
	}
	return result(ERR_OK);
}

/// Swap out the currently reserved tree for a new one and writes back the free
/// counter to the previously reserved global tree.
///
/// This checks first if the reserving flag has the expected value
/// returning ERR_RETRY if not.
static result_t swap_reserved(llc_t *self, local_t *local, reserved_t new,
			      bool expect_reserving)
{
	reserved_t old;
	if (!atom_update(&local->reserved, old, reserved_swap, new,
			 expect_reserving)) {
		info("swap resv failed");
		return result(ERR_RETRY);
	}

	if (old.present) {
		size_t tree_idx = tree_from_row(old.start_row);
		tree_t tree;
		if (!atom_update(&self->trees[tree_idx], tree, tree_writeback,
				 old.free)) {
			warn("Failed writeback %jd (next %" PRIu64 ")",
			     tree_idx, tree_from_row(new.start_row));
			return result(ERR_CORRUPTION);
		}
	}
	return result(ERR_OK);
}

/// Reserves a new tree and allocates with it.
/// Only if the allocation succeeds, the tree is fully reserved.
/// Otherwise the reservation is reverted.
static result_t reserve_tree_and_get(llc_t *self, local_t *local, size_t idx,
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
		reserved_t new = { .free = old_tree.free - (1 << order),
				   .start_row = row_from_pfn(res.val),
				   .present = true,
				   .reserving = false };
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

/// Searches the whole tree array starting at base_idx for a tree with
/// a free counter in the provided range, reserves it, and allocates from it.
static result_t search_global(llc_t *self, local_t *local, uint64_t base_idx,
			      uint64_t order, range_t free)
{
	// search outside of current cacheline for a partial tree
	for (int64_t i = 1; i <= (int64_t)self->trees_len; ++i) {
		int64_t toggle = i & 1 ? i / 2 : -i / 2;
		uint64_t idx = (base_idx + toggle) % self->trees_len;
		result_t res =
			reserve_tree_and_get(self, local, idx, order, free);
		if (res.val != ERR_MEMORY)
			return res;
	}
	return result(ERR_MEMORY);
}

/// Reserves a new tree and allocates from it.
///
/// The search for a new tree aims to be both fast and avoid fragmentation.
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

	uint64_t offset = start_idx % TREE_CHILDREN;
	uint64_t base_idx = start_idx - offset;
	uint64_t vicinity = div_ceil(div_ceil(self->trees_len, self->cores), 4);
	if (vicinity > TREE_CHILDREN)
		vicinity = TREE_CHILDREN;

	// search inside of current cacheline for a partial tree
	for (int64_t i = 1; i <= (int64_t)vicinity; ++i) {
		int64_t toggle = i & 1 ? i / 2 : -i / 2;
		uint64_t idx = (base_idx + toggle) % self->trees_len;
		result_t res =
			reserve_tree_and_get(self, local, idx, order,
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
		llc_drain(self, (local_idx + i) % self->cores);
	}

	// search whole tree for a tree with enough free frames
	res = search_global(self, local, base_idx, order,
			    (range_t){ 1 << order, TREESIZE });

	// clear reserving
	if (!result_ok(res)) {
		reserved_t old;
		bool _unused res = atom_update(&local->reserved, old,
					       reserved_set_reserving, false);
		assert(res);
	}
	return res;
}

static result_t reserve_or_wait(llc_t *self, size_t core, size_t order)
{
	local_t *local = get_local(self, core);
	reserved_t reserved;
	if (atom_update(&local->reserved, reserved, reserved_set_reserving,
			true)) {
		return reserve_and_get(self, local, core, order);
	}
	info("spin wait");
	while (({
		reserved_t r = atom_load(&local->reserved);
		r.reserving;
	})) {
		spin_wait();
	}
	return result(ERR_RETRY);
}

/// Synchronizes the free_counter of given local with the global counter.
static bool sync_with_global(llc_t *self, local_t *local, size_t order,
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

	info("undo sync global");
	// undo changes
	bool _unused ret = atom_update(&self->trees[tree_idx], old_tree,
				       tree_inc, desired.free);
	assert(ret);

	return false;
}

static result_t get_inner(llc_t *self, size_t core, size_t order)
{
	assert(self != NULL);
	assert(order <= MAX_ORDER);

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
			return result(ERR_RETRY); // Success -> Retry allocation
		}
		// The local tree is full -> reserve a new one
		return reserve_or_wait(self, core, order);
	}

	uint64_t start = pfn_from_row(reserved.start_row);

	result_t res = lower_get(&self->lower, start, order);
	if (result_ok(res)) {
		// save pfn only if necessary
		if (order <= ATOMIC_ORDER &&
		    reserved.start_row != row_from_pfn(res.val)) {
			atom_update(&local->reserved, reserved,
				    reserved_set_start, tree_from_pfn(res.val));
		}
		return res;
	}
	if (res.val == ERR_MEMORY) {
		// Increment global to prevent race condition with concurrent reservation
		tree_t old;
		if (atom_update(&self->trees[start / TREESIZE], old, tree_inc,
				1 << order)) {
			return reserve_or_wait(self, core, order);
		}

		warn("undo failed");
		return result(ERR_CORRUPTION);
	}
	return res;
}

result_t llc_get(llc_t *self, size_t core, size_t order)
{
	assert(self != NULL);
	assert(order <= MAX_ORDER);

	for (size_t i = 0; i < RETRIES; i++) {
		result_t res = get_inner(self, core, order);

		if (result_ok(res))
			return result(res.val + self->lower.offset);
		if (res.val != ERR_RETRY)
			return res;
	}

	warn("Exceeding retries");
	return result(ERR_MEMORY);
}

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
		warn("lower err %" PRIu64, res.val);
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
			result_t ret = swap_reserved(self, local, new, false);

			if (!result_ok(ret)) {
				if (ret.val != ERR_RETRY)
					return ret;

				if (!atom_update(&self->trees[tree_idx],
						 old_tree, tree_writeback,
						 old_tree.free)) {
					warn("Undo failed!");
					return result(ERR_CORRUPTION);
				}
			}
		}
	}
	return result(ERR_OK);
}

result_t llc_drain(llc_t *self, size_t core)
{
	local_t *local = get_local(self, core);

	reserved_t none = {
		.free = 0, .start_row = 0, .present = false, .reserving = false
	};
	result_t res = swap_reserved(self, local, none, false);
	if (result_ok(res) || res.val == ERR_RETRY)
		return result(ERR_OK);
	return res;
}

uint64_t llc_frames(llc_t *self)
{
	assert(self != NULL);
	return self->lower.frames;
}

uint64_t llc_free_frames(llc_t *self)
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

bool llc_is_free(llc_t *self, uint64_t frame, size_t order)
{
	assert(self != NULL);
	return lower_is_free(&self->lower, frame, order);
}

void llc_for_each_huge(llc_t *self, void *context,
		       void f(void *, uint64_t, uint64_t))
{
	assert(self != NULL);
	lower_for_each_child(&self->lower, context, f);
}

void llc_debug(llc_t *self, void (*writer)(void *, char *), void *arg)
{
	assert(self != NULL);

	char *msg = llc_ext_alloc(1, 200 * sizeof(char));
	snprintf(msg, 200, "LLC { frames: %" PRIu64 "/%jd, huge: %ju/%ju }",
		 llc_free_frames(self), self->lower.frames,
		 lower_free_huge(&self->lower), self->lower.childs_len);
	writer(arg, msg);
}

void llc_print(llc_t *self)
{
	printf("-----------------------------------------------\n"
	       "UPPER ALLOCATOR\nTrees:\t%ju\nCores:\t%ju\n allocated: %" PRIu64
	       ", free: %" PRIu64 ", all: %" PRIu64 "\n",
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
			printf("%ju\t", i);
	}
	printf("\nreserved:\t");
	for (size_t i = 0; i < self->trees_len; ++i) {
		if (i < 10 || i >= self->trees_len - 10) {
			tree_t tree = atom_load(&self->trees[i]);
			printf("%d\t", tree.reserved);
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
		printf("%ju\t", i);
	}
	printf("\nhas_tree:\t");
	for (size_t i = 0; i < self->cores; ++i) {
		reserved_t reserved = atom_load(&get_local(self, i)->reserved);
		printf("%d\t", reserved.present);
	}
	printf("\nTreeIDX:\t");
	for (size_t i = 0; i < self->cores; ++i) {
		reserved_t reserved = atom_load(&get_local(self, i)->reserved);
		printf("%" PRIu64 "\t", tree_from_row(reserved.start_row));
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

void llc_drop(llc_t *self)
{
	assert(self != NULL);

	if (self->meta != NULL)
		self->meta->crashed = false;

	// if initialized
	if (self->local != NULL) {
		lower_drop(&self->lower);
		llc_ext_free(CACHE_SIZE, self->trees_len * sizeof(tree_t),
			     self->trees);
		llc_ext_free(CACHE_SIZE, self->cores * sizeof(local_t),
			     self->local);
	}
}
