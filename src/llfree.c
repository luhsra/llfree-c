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

static inline uint8_t tree_kind(llfree_t *self, llflags_t flags)
{
	// only separate if enough trees
	if (self->trees_len < (TREE_KINDS * self->cores))
		return TREE_FIXED;

	if (flags.order >= LLFREE_HUGE_ORDER)
		return TREE_HUGE;
	if (flags.movable)
		return TREE_MOVABLE;
	return TREE_FIXED;
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
		self->trees[tree_idx] = tree_new(sum, false, TREE_FIXED);
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
			  uint8_t kind)
{
	reserved_t old = atom_swap(&local->reserved[kind], new);
	if (old.present) {
		size_t tree_idx = tree_from_row(old.start_row);
		uint16_t free = old.free;
		tree_t tree;
		atom_update(&self->trees[tree_idx], tree, tree_unreserve, free,
			    kind);
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

	uint8_t kind = tree_kind(self, flags);

	tree_t old;
	if (!atom_update(&self->trees[idx], old, tree_reserve, free.min,
			 free.max, kind))
		return llfree_result(LLFREE_ERR_MEMORY);
	assert(!old.reserved && old.free >= (1 << flags.order));

	llfree_result_t res =
		lower_get(&self->lower, pfn_from_tree(idx), flags);

	if (llfree_ok(res)) {
		// write back llfree_result
		reserved_t new = { .free = old.free -
					   (uint16_t)(1 << flags.order),
				   .start_row = row_from_pfn((uint64_t)res.val),
				   .present = true };
		swap_reserved(self, local, new, kind);
	} else {
		// undo reservation
		tree_t tree;
		atom_update(&self->trees[idx], tree, tree_unreserve, old.free,
			    kind);
	}
	return res;
}

/// Searches the tree array for a tree with a free counter in the provided range,
/// reserves it, and allocates from it.
static llfree_result_t search(llfree_t *self, local_t *local, uint64_t start,
			      llflags_t flags, uint64_t offset, uint64_t len,
			      p_range_t free)
{
	if (free.min >= free.max)
		return llfree_result(LLFREE_ERR_MEMORY);

	free.min = LL_MAX(free.min, (uint16_t)(1 << flags.order));

	int64_t base = (int64_t)(start + self->trees_len);
	for (int64_t i = (int64_t)offset; i < (int64_t)len; ++i) {
		// Search alternating left and right from start
		int64_t off = i % 2 == 0 ? i / 2 : -((i + 1) / 2);
		uint64_t idx = (uint64_t)(base + off) % self->trees_len;
		llfree_result_t res =
			reserve_tree_and_get(self, local, idx, flags, free);
		if (res.val != LLFREE_ERR_MEMORY)
			return res;
	}
	return llfree_result(LLFREE_ERR_MEMORY);
}

/// Steal from other core
static llfree_result_t steal_tree(llfree_t *self, size_t core, llflags_t flags)
{
	uint8_t kind = tree_kind(self, flags);
	local_t *my_local = get_local(self, core);

	for (size_t c = 1; c < self->cores; c++) {
		size_t target_core = (core + c) % self->cores;
		local_t *local = get_local(self, target_core);
		reserved_t old;
		if (atom_update(&local->reserved[kind], old, ll_steal,
				(uint16_t)(1 << flags.order))) {
			llfree_result_t res =
				lower_get(&self->lower,
					  pfn_from_row(old.start_row), flags);
			if (llfree_ok(res)) {
				reserved_t new = {
					.free = old.free -
						(uint16_t)(1 << flags.order),
					.start_row =
						row_from_pfn((uint64_t)res.val),
					.present = true,
				};
				swap_reserved(self, my_local, new, kind);
				return res;
			}
			assert(res.val == LLFREE_ERR_MEMORY);
		}
	}
	return llfree_result(LLFREE_ERR_MEMORY);
}

/// Reserves a new tree and allocates from it.
///
/// The search for a new tree aims to be both fast and avoid fragmentation.
static _unused llfree_result_t reserve_and_get(llfree_t *self, size_t core,
					       llflags_t flags, reserved_t old)
{
	const size_t cl_trees = LLFREE_CACHE_SIZE / sizeof(tree_t);

	local_t *local = get_local(self, core);
	llfree_result_t res;

	uint64_t start = old.present ? tree_from_row(old.start_row) :
				       (self->trees_len / self->cores * core);
	assert(start < self->trees_len);
	start = align_down(start, cl_trees);

	size_t near = (self->trees_len / self->cores) / 4;
	near = LL_MIN(LL_MAX(near, cl_trees / 4), cl_trees * 2);

	// Over half filled trees
	p_range_t half = { LL_MAX(LLFREE_TREE_SIZE / 16,
				  (uint16_t)(2 << flags.order)),
			   LLFREE_TREE_SIZE / 2 };
	res = search(self, local, start, flags, 1, near, half);
	if (res.val != LLFREE_ERR_MEMORY)
		return res;

	// Partially filled tree
	p_range_t partial = { LL_MAX(LLFREE_TREE_SIZE / 64,
				     (uint16_t)(2 << flags.order)),
			      LLFREE_TREE_SIZE - LLFREE_TREE_SIZE / 16 };
	res = search(self, local, start, flags, 1, 2 * near, partial);
	if (res.val != LLFREE_ERR_MEMORY)
		return res;

	// Not free tree
	p_range_t notfree = { 0, LLFREE_TREE_SIZE - 4 };
	res = search(self, local, start, flags, 1, 4 * near, notfree);
	if (res.val != LLFREE_ERR_MEMORY)
		return res;

	// Any tree
	p_range_t any = { 0, LLFREE_TREE_SIZE };
	res = search(self, local, start, flags, 0, self->trees_len, any);
	if (res.val != LLFREE_ERR_MEMORY)
		return res;

	// Steal from other core
	res = steal_tree(self, core, flags);
	if (res.val == LLFREE_ERR_MEMORY)
		return llfree_result(LLFREE_ERR_RETRY);

	return res;
}

/// Synchronizes the free_counter of given local with the global counter.
static bool sync_with_global(llfree_t *self, local_t *local, llflags_t flags,
			     reserved_t old)
{
	assert(self != NULL && local != NULL);
	uint8_t kind = tree_kind(self, flags);

	if (!old.present || old.free >= (1 << flags.order))
		return false;

	size_t tree_idx = tree_from_row(old.start_row);
	uint16_t min_free = (uint16_t)(1 << flags.order) - old.free;

	tree_t old_tree;
	if (!atom_update(&self->trees[tree_idx], old_tree, tree_steal_counter,
			 min_free, kind))
		return false;

	reserved_t new = { .free = old.free + old_tree.free,
			   .start_row = old.start_row,
			   .present = true };
	if (!atom_cmp_exchange(&local->reserved[kind], &old, new)) {
		// Revert the steal
		atom_update(&self->trees[tree_idx], old_tree, tree_inc,
			    old_tree.free);
		return false;
	}
	return true;
}

static llfree_result_t get_inner(llfree_t *self, size_t core, llflags_t flags)
{
	assert(self != NULL);
	assert(flags.order <= LLFREE_MAX_ORDER);

	local_t *local = get_local(self, core);

	uint8_t kind = tree_kind(self, flags);

	reserved_t old;
	// Try decrementing the local counter
	if (atom_update(&local->reserved[kind], old, ll_reserved_dec,
			(uint16_t)(1 << flags.order))) {
		llfree_result_t res = lower_get(
			&self->lower, pfn_from_row(old.start_row), flags);
		if (llfree_ok(res)) {
			if (old.start_row != row_from_pfn((uint64_t)res.val)) {
				reserved_t old;
				atom_update(&local->reserved[kind], old,
					    ll_reserved_set_start,
					    row_from_pfn((uint64_t)res.val));
			}
			return res;
		}

		// Undo decrement (inc global tree)
		tree_t tree;
		atom_update(&self->trees[tree_from_row(old.start_row)], tree,
			    tree_inc, (uint16_t)(1 << flags.order));

		if (res.val == LLFREE_ERR_MEMORY) {
			// Current tree is fragmented!
			return reserve_and_get(self, core, flags, old);
		}
		return res;
	}
	// Try synchronizing with global counter
	if (old.present && sync_with_global(self, local, flags, old)) {
		// Success -> Retry allocation
		return llfree_result(LLFREE_ERR_RETRY);
	}

	return reserve_and_get(self, core, flags, old);
}

llfree_result_t llfree_get(llfree_t *self, size_t core, llflags_t flags)
{
	assert(self != NULL);
	assert(flags.order <= LLFREE_MAX_ORDER);

	core = core % self->cores;

	for (size_t i = 0; i < RETRIES; i++) {
		llfree_result_t res = get_inner(self, core, flags);

		if (res.val != LLFREE_ERR_RETRY)
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
	core = core % self->cores;
	local_t *local = get_local(self, core);

	size_t tree_idx = tree_from_pfn(frame);

	// Update free-reserve heuristic
	bool reserve = ll_local_free_inc(local, tree_idx);

	// Increment local or otherwise global counter
	if (flags.order >= LLFREE_HUGE_ORDER) {
		uint8_t kind = tree_kind(self, llflags(LLFREE_HUGE_ORDER));
		reserved_t old;
		if (atom_update(&local->reserved[kind], old, ll_reserved_inc,
				tree_idx, (uint16_t)(1 << flags.order))) {
			return llfree_result(LLFREE_ERR_OK);
		}
	} else {
		int kinds[2] = { TREE_MOVABLE, TREE_FIXED };
		for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
			reserved_t old;
			if (atom_update(&local->reserved[kinds[i]], old,
					ll_reserved_inc, tree_idx,
					(uint16_t)(1 << flags.order))) {
				return llfree_result(LLFREE_ERR_OK);
			}
		}
	}

	tree_t old_t;
	atom_update(&self->trees[tree_idx], old_t, tree_inc_or_reserve,
		    (uint16_t)(1 << flags.order), &reserve, TREE_LOWER_LIM);

	// Free-reserve heuristic:
	// Reserve trees where a lot of frees happen, assuming locality
	if (reserve) {
		// fully reserve the new tree
		uint16_t free = old_t.free + (uint16_t)(1 << flags.order);
		assert(!old_t.reserved && free <= LLFREE_TREE_SIZE);

		reserved_t new = { .free = free,
				   .start_row = row_from_tree(tree_idx),
				   .present = true };

		uint8_t kind = free < LLFREE_TREE_SIZE ? old_t.kind :
							 tree_kind(self, flags);
		swap_reserved(self, local, new, kind);
	}
	return llfree_result(LLFREE_ERR_OK);
}

llfree_result_t llfree_drain(llfree_t *self, size_t core)
{
	local_t *local = get_local(self, core);
	reserved_t none = {
		.free = 0,
		.start_row = 0,
		.present = false,
	};
	swap_reserved(self, local, none, TREE_FIXED);
	swap_reserved(self, local, none, TREE_MOVABLE);
	swap_reserved(self, local, none, TREE_HUGE);
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
		local_t *local = get_local(self, core);
		for (size_t kind = TREE_FIXED; kind < TREE_KINDS; kind++) {
			reserved_t reserved = atom_load(&local->reserved[kind]);
			if (reserved.present)
				free += reserved.free;
		}
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
	snprintf(msg, sizeof(msg),
		 "LLC { cores: %" PRIuS ", frames: %" PRIuS "/%" PRIuS
		 ", huge: %" PRIuS "/%" PRIuS ", trees: %" PRIuS "/%" PRIuS
		 " }\n",
		 self->cores, llfree_free_frames(self), self->lower.frames,
		 lower_free_huge(&self->lower),
		 div_ceil(self->lower.frames, LLFREE_CHILD_SIZE), free_trees,
		 self->trees_len);
	writer(arg, msg);
}

#define I1 "    "
#define I2 I1 I1
#define I3 I2 I1

void llfree_print(llfree_t *self)
{
	llfree_info_start();
	llfree_info_cont("llfree_t {\n");
	llfree_info_cont(I1 "frames: %" PRIuS "\n", self->lower.frames);
	llfree_info_cont(I1 "trees: %" PRIuS " (%u) {\n", self->trees_len,
			 LLFREE_TREE_SIZE);
	size_t _unused free_huge = llfree_free_huge(self);
	size_t _unused free_trees = 0;
	for (size_t i = 0; i < self->trees_len; i++) {
		tree_t tree = atom_load(&self->trees[i]);
		llfree_info_cont(I2 "%3" PRIuS ": free: %" PRIuS
				    ", reserved: %d\n",
				 i, (size_t)tree.free, tree.reserved);
		if (tree.free == LLFREE_TREE_SIZE)
			free_trees += 1;
	}
	llfree_info_cont(I1 "}\n");

	llfree_info_cont(I1 "free: { frames: %" PRIuS ", huge: %" PRIuS
			    ", trees: %" PRIuS " }\n",
			 llfree_free_frames(self), free_huge, free_trees);

	for (size_t i = 0; i < self->cores; i++) {
		llfree_info_cont(I1 "%" PRIuS ": local: {\n", i);
		for (size_t kind = TREE_FIXED; kind < TREE_KINDS; kind++) {
			reserved_t _unused res =
				atom_load(&self->local[i].reserved[kind]);
			llfree_info_cont(I2 "%" PRIuS
					    ": { present: %d, free: %" PRIu64
					    ", start: %" PRIu64 "}\n",
					 kind, res.present, (uint64_t)res.free,
					 tree_from_row(res.start_row));
		}
		llfree_info_cont(I1 "}\n");
	}
	llfree_info_cont("}");
	llfree_info_end();
}

// clang-format off
#define fmt_spec(x) _Generic((x),		\
		_Bool: "%d", 			\
		char: "%c",			\
		unsigned char: "%c",		\
		short: "%hd",			\
		unsigned short: "%hu",		\
		int: "%d",			\
		unsigned int: "%u",		\
		long: "%ld",			\
		unsigned long: "%lu",		\
		long long: "%lld",		\
		unsigned long long: "%llu"	\
	)
// clang-format on

#define check(x)                            \
	if (!(x)) {                         \
		llfree_info_start();        \
		llfree_info_cont("failed"); \
		llfree_info_end();          \
		assert(false);              \
	}                                   \
	(void)0 // enforce semicolon!

#define check_equal(actual, expected)                             \
	if ((actual) != (expected)) {                             \
		llfree_info_start();                              \
		llfree_info_cont("failed: ");                     \
		llfree_info_cont(fmt_spec(actual), (actual));     \
		llfree_info_cont(" == ");                         \
		llfree_info_cont(fmt_spec(expected), (expected)); \
		llfree_info_end();                                \
		assert(false);                                    \
	}                                                         \
	(void)0 // enforce semicolon!

void llfree_validate(llfree_t *self)
{
	check_equal(llfree_free_frames(self), lower_free_frames(&self->lower));
	check_equal(llfree_free_huge(self), lower_free_huge(&self->lower));

	for (size_t tree_idx = 0; tree_idx < self->trees_len; tree_idx++) {
		tree_t tree = atom_load(&self->trees[tree_idx]);
		check(tree.free <= LLFREE_TREE_SIZE);
		if (!tree.reserved) {
			size_t free = lower_free_at_tree(
				&self->lower, pfn_from_tree(tree_idx));
			check_equal(free, tree.free);
		}
	}

	for (size_t core = 0; core < self->cores; core++) {
		local_t *local = get_local(self, core);
		for (size_t kind = TREE_FIXED; kind < TREE_KINDS; kind++) {
			reserved_t res = atom_load(&local->reserved[kind]);
			if (res.present) {
				size_t tree_idx = tree_from_row(res.start_row);
				tree_t tree = atom_load(&self->trees[tree_idx]);
				check(tree.reserved);
				check(tree.free <= LLFREE_TREE_SIZE);
				size_t free = lower_free_at_tree(
					&self->lower, pfn_from_tree(tree_idx));
				check_equal(tree.free + res.free, free);
			}
		}
	}
}
