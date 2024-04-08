#include "llfree_inner.h"

#include "check.h"
#include "local.h"
#include "lower.h"

#include <memory.h>
#include <pthread.h>
#include <stdlib.h>

void print_trees(llfree_t *self)
{
	printf("trees:\ti\tflag\tcounter\n");
	for (size_t i = 0; i < self->trees_len; ++i) {
		tree_t _unused tree = atom_load(&self->trees[i]);
		llfree_info("\t%zu\t%d\t%X\n", i, tree.reserved, tree.free);
	}
}

static llfree_t llfree_new(size_t cores, size_t frames, uint8_t init)
{
	llfree_t upper;
	llfree_meta_size_t m = llfree_metadata_size(cores, frames);
	uint8_t *primary = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.primary);
	uint8_t *secondary = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.secondary);
	assert(primary != NULL && secondary != NULL);
	llfree_result_t _unused ret =
		llfree_init(&upper, cores, frames, init, primary, secondary);
	assert(llfree_ok(ret));
	return upper;
}

static void llfree_drop(llfree_t *self)
{
	llfree_meta_size_t ms =
		llfree_metadata_size(self->cores, llfree_frames(self));
	llfree_meta_t m = llfree_metadata(self);
	llfree_ext_free(LLFREE_CACHE_SIZE, ms.primary, m.primary);
	llfree_ext_free(LLFREE_CACHE_SIZE, ms.secondary, m.secondary);
}

declare_test(llfree_init)
{
	bool success = true;

	llfree_t upper = llfree_new(4, 1 << 20, LLFREE_INIT_FREE);
	check(llfree_frames(&upper) == 1 << 20);
	check(llfree_free_frames(&upper) == 1 << 20);

	llfree_validate(&upper);

	return success;
}

declare_test(llfree_init_alloc)
{
	bool success = true;

	const size_t FRAMES = (1 << 30) / LLFREE_FRAME_SIZE;

	llfree_t upper = llfree_new(4, FRAMES, LLFREE_INIT_ALLOC);
	check(llfree_frames(&upper) == FRAMES);
	check(llfree_free_frames(&upper) == 0);

	for (size_t hp = 0; hp < (FRAMES >> LLFREE_HUGE_ORDER) - 1; hp++) {
		check(llfree_ok(llfree_put(&upper, 0, hp << LLFREE_HUGE_ORDER,
					   llflags(LLFREE_HUGE_ORDER))));
	}
	for (size_t page = FRAMES - (1 << LLFREE_HUGE_ORDER); page < FRAMES;
	     page++) {
		check(llfree_ok(llfree_put(&upper, 0, page, llflags(0))));
	}
	check(llfree_free_frames(&upper) == FRAMES);

	llflags_t llf = llflags(0);
	llf.movable = true;
	check(llfree_ok(llfree_get(&upper, 0, llf)));

	check(llfree_ok(llfree_get(&upper, 0, llflags(0))));

	check(llfree_ok(llfree_get(&upper, 0, llf)));

	llfree_validate(&upper);

	return success;
}

declare_test(llfree_alloc_s)
{
	bool success = true;
	const size_t FRAMES = (1ul << 30) / LLFREE_FRAME_SIZE;

	llfree_info("Init");
	llfree_t upper = llfree_new(4, FRAMES, LLFREE_INIT_FREE);
	llfree_validate(&upper);

	llfree_info("Alloc");
	size_t n = 128;
	for (size_t i = 0; i < n; i++) {
		check(llfree_ok(llfree_get(&upper, 0, llflags(0))));
		check(llfree_ok(
			llfree_get(&upper, 0, llflags(LLFREE_HUGE_ORDER))));
	}

	check_equal(n + (n << LLFREE_HUGE_ORDER),
		    llfree_frames(&upper) - llfree_free_frames(&upper));
	check_equal(n + (n << LLFREE_HUGE_ORDER),
		    upper.lower.frames - lower_free_frames(&upper.lower));

	check_equal(llfree_free_frames(&upper),
		    lower_free_frames(&upper.lower));

	for (size_t i = 0; i < upper.trees_len; i++) {
		size_t free = 0;
		for (size_t j = 0; j < LLFREE_TREE_CHILDREN; j++) {
			free += lower_free_at_huge(
				&upper.lower,
				i * LLFREE_TREE_SIZE + j * LLFREE_CHILD_SIZE);
		}

		tree_t tree = atom_load(&upper.trees[i]);
		if (!tree.reserved)
			check_equal_m(free, tree.free, "tree %lu", i);
	}

	llfree_validate(&upper);

	llfree_drop(&upper);
	return success;
}

declare_test(llfree_general_function)
{
	bool success = true;
	llfree_result_t ret;

	llfree_t upper = llfree_new(4, 132000, LLFREE_INIT_FREE);
	llfree_validate(&upper);
	check_equal(upper.trees_len, div_ceil(132000, LLFREE_TREE_SIZE));
	check(upper.cores == 4);
	check(llfree_frames(&upper) == 132000);
	check_m(llfree_free_frames(&upper) == 132000,
		"right number of free frames");

	// check allignment
	check_m((uint64_t)upper.trees % LLFREE_CACHE_SIZE == 0,
		"Alignment of trees");
	for (unsigned i = 0; i < upper.cores; ++i) {
		check_equal_m((uint64_t)&upper.local[i] % LLFREE_CACHE_SIZE,
			      0ul, "Alignment of local");
	}

	llfree_info("Before get");

	llfree_result_t frame = llfree_get(&upper, 0, llflags(0));
	check_m(llfree_ok(frame), "reservation must be success");
	llfree_validate(&upper);

	check(llfree_frames(&upper) == 132000);
	check_m(llfree_free_frames(&upper) == 131999,
		"right number of free frames");

	llfree_info("After get mit core 0\n");

	ret = llfree_put(&upper, 0, (uint64_t)frame.val, llflags(0));
	llfree_validate(&upper);

	check_m(llfree_ok(ret), "successfully free");
	check_m(llfree_free_frames(&upper) == 132000,
		"right number of free frames");

	local_history_t last = atom_load(&upper.local[0].last);
	check_equal(last.idx, tree_from_pfn(frame.val));

	// reserve all frames in first tree
	for (size_t i = 0; i < LLFREE_TREE_SIZE; ++i) {
		ret = llfree_get(&upper, 0, llflags(0));
		check(llfree_ok(ret));
	}

	llfree_validate(&upper);

	// reserve first frame in new tree
	ret = llfree_get(&upper, 0, llflags(0));
	check(llfree_ok(ret));
	check_m(tree_from_pfn(frame.val) != tree_from_pfn(ret.val),
		"second tree must be allocated");

	uint64_t free_frames = llfree_free_frames(&upper);
	// reserve and free a HugeFrame
	frame = llfree_get(&upper, 0, llflags(LLFREE_HUGE_ORDER));
	check(llfree_ok(frame));
	check_equal(llfree_free_frames(&upper),
		    free_frames - LLFREE_CHILD_SIZE);
	check(llfree_ok(llfree_put(&upper, 0, (uint64_t)frame.val,
				   llflags(LLFREE_HUGE_ORDER))));
	check_equal(llfree_free_frames(&upper), free_frames);

	llfree_validate(&upper);

	llfree_drop(&upper);
	return success;
}

declare_test(llfree_put)
{
	bool success = true;
	llfree_result_t ret;

	llfree_t upper = llfree_new(4, LLFREE_TREE_SIZE << 4, LLFREE_INIT_FREE);
	llfree_validate(&upper);
	uint64_t reserved[LLFREE_TREE_SIZE + 5];

	// reserve more frames than one tree
	for (size_t i = 0; i < LLFREE_TREE_SIZE + 5; ++i) {
		ret = llfree_get(&upper, 1, llflags(0));
		check(llfree_ok(ret));
		reserved[i] = (uint64_t)ret.val;
	}

	check_equal(tree_from_pfn(reserved[0]),
		    tree_from_pfn(reserved[LLFREE_TREE_SIZE - 1]));
	check(tree_from_pfn(reserved[0]) !=
	      tree_from_pfn(reserved[LLFREE_TREE_SIZE]));

	llfree_result_t ret2 = llfree_get(&upper, 2, llflags(0));
	check(llfree_ok(ret2));
	check_m(tree_from_pfn(ret2.val) !=
			tree_from_pfn(reserved[LLFREE_TREE_SIZE]),
		"second get must be in different tree");

	if (!success)
		llfree_print(&upper);

	llfree_info("free");

	// free half the frames from old tree with core 2
	for (size_t i = 0; i < LLFREE_TREE_SIZE / 2; ++i) {
		ret = llfree_put(&upper, 2, reserved[i], llflags(0));
		check(llfree_ok(ret));
	}

	// core 2 must have now this first tree reserved
	reserved_t res = upper.local[2].reserved[0];
	check_equal(tree_from_row(res.start_row),
		    (uint64_t)tree_from_pfn(reserved[0]));
	if (!success)
		llfree_print(&upper);
	llfree_validate(&upper);
	llfree_drop(&upper);

	return success;
}

declare_test(llfree_alloc_all)
{
	bool success = true;
	llfree_result_t ret;

	const int CORES = 8;
	const uint64_t LENGTH = ((8ul << 30) / LLFREE_FRAME_SIZE); // 8GiB
	llfree_t upper = llfree_new(CORES, LENGTH, LLFREE_INIT_FREE);
	llfree_validate(&upper);

	int64_t *pfns = malloc(sizeof(int64_t) * LENGTH);
	assert(pfns != NULL);

	for (size_t i = 0; i < LENGTH; ++i) {
		llfree_result_t ret = llfree_get(&upper, i % CORES, llflags(0));
		check_m(llfree_ok(ret),
			"must be able to alloc the whole memory");
		if (!llfree_ok(ret)) {
			llfree_print(&upper);
			break;
		}
		pfns[i] = ret.val;
	}
	ret = llfree_get(&upper, 0, llflags(0));
	check(ret.val == LLFREE_ERR_MEMORY);

	check_equal(llfree_free_frames(&upper), 0ul);
	check_equal(llfree_free_frames(&upper),
		    lower_free_frames(&upper.lower));
	llfree_validate(&upper);

	return success;
}

struct arg {
	size_t core;
	size_t order;
	size_t amount;
	size_t allocations;
	llfree_t *upper;
};
struct ret {
	uint64_t *pfns;
	size_t sp;
};

static int64_t contains(uint64_t const *sorted_list, size_t len, uint64_t item)
{
	if (item < sorted_list[0] || item > sorted_list[len - 1])
		return -1;

	for (size_t i = 0; i < len; ++i) {
		if (sorted_list[i] == item)
			return (int64_t)i;
		if (sorted_list[i] > item)
			return -1;
	}
	return -1;
}

static int comp(const void *a, const void *b)
{
	int64_t *x = (int64_t *)a;
	int64_t *y = (int64_t *)b;

	return (int)(*x - *y);
}

static void *alloc_frames(void *arg)
{
	struct arg *args = arg;

	struct ret *ret = malloc(sizeof(struct ret));
	assert(ret != NULL);

	ret->pfns = malloc(args->amount * sizeof(int64_t));
	assert(ret->pfns != NULL);

	srand((unsigned int)args->core);
	for (size_t i = 0; i < args->allocations; ++i) {
		// if full or in 1/3 times free a random reserved frame;
		if (ret->sp == args->amount ||
		    (ret->sp > 0 && rand() % 8 > 4)) {
			size_t random_idx = (size_t)rand() % ret->sp;
			llfree_result_t res = llfree_put(args->upper,
							 args->core,
							 ret->pfns[random_idx],
							 llflags(args->order));
			assert(llfree_ok(res));
			--ret->sp;
			ret->pfns[random_idx] = ret->pfns[ret->sp];
			--i;
		} else {
			llfree_result_t res = llfree_get(
				args->upper, args->core, llflags(args->order));
			assert(llfree_ok(res));
			ret->pfns[ret->sp] = (uint64_t)res.val;
			++ret->sp;
		}
	}
	qsort(ret->pfns, ret->sp, sizeof(int64_t), comp);
	return ret;
}

declare_test(llfree_parallel_alloc)
{
#undef CORES
#define CORES 4

	bool success = true;

	const uint64_t LENGTH = 16 << 18;
	llfree_t upper = llfree_new(CORES, LENGTH, LLFREE_INIT_FREE);
	llfree_validate(&upper);

	pthread_t threads[CORES];
	struct arg args[CORES];
	for (size_t i = 0; i < CORES; ++i) {
		args[i] = (struct arg){ .core = i,
					.order = 0,
					.amount = (LLFREE_TREE_SIZE + 500),
					.allocations = 40000,
					.upper = &upper };
		assert(pthread_create(&threads[i], NULL, alloc_frames,
				      &args[i]) == 0);
	}

	struct ret *rets[CORES];
	for (int i = 0; i < CORES; ++i) {
		assert(pthread_join(threads[i], (void **)&rets[i]) == 0);
		assert(rets[i] != NULL);
	}

	size_t still_reserved = 0;
	for (int i = 0; i < CORES; ++i) {
		still_reserved += rets[i]->sp;
	}
	// now all threads are terminated
	check_equal(llfree_frames(&upper) - llfree_free_frames(&upper),
		    still_reserved);
	check_equal(llfree_free_frames(&upper),
		    lower_free_frames(&upper.lower));
	llfree_validate(&upper);

	// duplicate check
	for (size_t core = 0; core < CORES; ++core) {
		for (size_t i = core + 1; i < CORES; ++i) {
			for (size_t idx = 0; idx < rets[core]->sp; ++idx) {
				uint64_t frame = rets[core]->pfns[idx];
				int64_t same = contains(rets[i]->pfns,
							rets[i]->sp, frame);
				if (same <= 0)
					continue;

				success = false;
				printf("\tFound duplicate reserved Frame\n both core %zu and %zu "
				       "reserved frame %" PRId64
				       " in tree %" PRId64 "\n",
				       core, i, frame, tree_from_pfn(frame));
				// leave all loops
				goto end;
			}
		}
	}
end:

	if (!success) {
		llfree_print(&upper);
	}
	for (size_t i = 0; i < CORES; ++i) {
		free(rets[i]->pfns);
		free(rets[i]);
	}
	llfree_validate(&upper);
	llfree_drop(&upper);
	return success;
}

struct par_args {
	llfree_t *upper;
	uint64_t **frames;
	size_t core;
};

#undef CORES
#define CORES 4

static void *parallel_alloc_all(void *input)
{
	struct par_args *args = input;

	size_t total = llfree_frames(args->upper);
	*(args->frames) =
		llfree_ext_alloc(LLFREE_FRAME_SIZE, total * sizeof(uint64_t));

	size_t i = 0;
	for (;; i++) {
		assert(i < total);

		llfree_result_t ret =
			llfree_get(args->upper, args->core, llflags(0));
		if (!llfree_ok(ret)) {
			assert(ret.val == LLFREE_ERR_MEMORY);
			break;
		}
		assert((uint64_t)ret.val < total);

		(*(args->frames))[i] = (uint64_t)ret.val;
	}
	llfree_info("finish %zu (%zu)", args->core, i);
	return (void *)i;
}

declare_test(llfree_parallel_alloc_all)
{
	bool success = true;
	const uint64_t LENGTH = 8 << 18; // 8G

	llfree_t upper = llfree_new(CORES, LENGTH, LLFREE_INIT_FREE);

	uint64_t *frames[CORES] = { NULL };
	struct par_args args[CORES];

	pthread_t threads[CORES];
	for (size_t i = 0; i < CORES; i++) {
		llfree_info("spawn %zu", i);
		args[i] = (struct par_args){ &upper, &frames[i], i };
		assert(pthread_create(&threads[i], NULL, parallel_alloc_all,
				      &args[i]) == 0);
	}

	size_t counts[CORES] = { 0 };
	uint64_t *all_frames = llfree_ext_alloc(
		LLFREE_FRAME_SIZE, llfree_frames(&upper) * sizeof(uint64_t));

	size_t total = 0;
	for (size_t i = 0; i < CORES; i++) {
		llfree_info("wait for %zu", i);
		assert(pthread_join(threads[i], (void **)&counts[i]) == 0);

		// collect allocated pages
		memcpy(&all_frames[total], frames[i],
		       counts[i] * sizeof(uint64_t));
		llfree_ext_free(sizeof(uint64_t), llfree_frames(&upper),
				frames[i]);
		total += counts[i];
	}

	check_equal(llfree_frames(&upper), total);
	check_equal(llfree_free_frames(&upper),
		    lower_free_frames(&upper.lower));

	// check for duplicates
	qsort(all_frames, total, sizeof(int64_t), comp);
	for (size_t i = 1; i < total; i++) {
		check_m(all_frames[i] < llfree_frames(&upper), "check %" PRIx64,
			all_frames[i]);
		check_m(all_frames[i - 1] != all_frames[i],
			"dup %" PRIx64 " at %zu", all_frames[i], i);
	}

	llfree_validate(&upper);
	llfree_drop(&upper);
	return success;
}

declare_test(llfree_get_huge)
{
	bool success = true;
	llfree_result_t res;
	llfree_t upper = llfree_new(1, LLFREE_TREE_SIZE, LLFREE_INIT_FREE);

	llfree_info("get");
	res = llfree_get(&upper, 0, llflags(LLFREE_HUGE_ORDER));
	check(llfree_ok(res));

	llfree_info("get");
	res = llfree_get(&upper, 0, llflags(LLFREE_HUGE_ORDER));
	check(llfree_ok(res));

	llfree_validate(&upper);
	llfree_drop(&upper);

	return success;
}

struct llfree_less_mem {
	_Atomic(uint64_t) sync0;
	_Atomic(uint64_t) sync1;
	llfree_t *llfree;
	size_t cores;
	size_t frames_len;
};

struct llfree_less_mem_p {
	struct llfree_less_mem *shared;
	size_t core;
};

static void *llfree_less_mem_par(void *input)
{
	struct llfree_less_mem_p *data = input;
	struct llfree_less_mem *shared = data->shared;

	uint64_t *frames = llfree_ext_alloc(
		sizeof(uint64_t), sizeof(uint64_t) * shared->frames_len);

	llfree_info("sync0 on %zu", data->core);
	atomic_fetch_add(&shared->sync0, 1);
	while (atom_load(&shared->sync0) < shared->cores) {
		spin_wait();
	}

	for (size_t i = 0; i < shared->frames_len; i++) {
		llfree_result_t res =
			llfree_get(shared->llfree, data->core, llflags(0));
		assert(llfree_ok(res));
		frames[i] = (uint64_t)res.val;
	}

	llfree_info("sync1 on %zu", data->core);
	atomic_fetch_add(&shared->sync1, 1);
	while (atom_load(&shared->sync1) < shared->cores) {
		spin_wait();
	}

	for (size_t i = 0; i < shared->frames_len; i++) {
		llfree_result_t res = llfree_put(shared->llfree, data->core,
						 frames[i], llflags(0));
		assert(llfree_ok(res));
	}

	return NULL;
}

declare_test(llfree_less_mem)
{
	bool success = true;
#undef CORES
#define CORES 4
#undef FRAMES
#define FRAMES 4096

	llfree_t upper = llfree_new(CORES, FRAMES, LLFREE_INIT_FREE);

	llfree_info("get");

	struct llfree_less_mem shared = { .sync0 = 0,
					  .sync1 = 0,
					  .llfree = &upper,
					  .cores = CORES,
					  .frames_len = FRAMES / CORES };
	struct llfree_less_mem_p private[CORES];
	pthread_t threads[CORES];
	for (size_t i = 0; i < CORES; i++) {
		private[i].core = i;
		private[i].shared = &shared;
		assert(pthread_create(&threads[i], NULL, llfree_less_mem_par,
				      &private[i]) == 0);
	}

	for (size_t i = 0; i < CORES; i++) {
		assert(pthread_join(threads[i], NULL) == 0);
	}

	check_equal(llfree_free_frames(&upper), FRAMES);
	llfree_validate(&upper);
	llfree_drop(&upper);

	return success;
}
