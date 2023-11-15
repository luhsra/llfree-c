#include "child.h"
#include "llfree_inner.h"

#include "check.h"
#include "local.h"
#include "lower.h"
#include "utils.h"

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

bool check_init(llfree_t *upper, size_t cores, uint64_t offset, size_t len,
		uint8_t init, uint8_t free_all)
{
	bool success = true;
	llfree_result_t ret =
		llfree_init(upper, cores, offset, len, init, free_all);
	size_t num_trees = div_ceil(len, LLFREE_TREE_SIZE);
	size_t num_childs = div_ceil(len, LLFREE_CHILD_SIZE);
	check_equal((uint64_t)upper->trees % LLFREE_CACHE_SIZE, 0ull);

	check_m(llfree_ok(ret), "init is success");
	check_equal(upper->trees_len, num_trees);
	check_equal(upper->lower.childs_len, num_childs);
	check_equal(upper->lower.offset, offset);
	check_equal(upper->cores, (cores > num_trees ? num_trees : cores));
	check_equal(llfree_frames(upper), len);
	check_equal(llfree_free_frames(upper), free_all ? len : 0);
	reserved_t reserved = atom_load(&upper->local[0].reserved);
	check(!reserved.present);

	return success;
}

declare_test(llfree_init)
{
	int success = true;

	llfree_t *upper = llfree_ext_alloc(LLFREE_CACHE_SIZE, sizeof(llfree_t));
	check_m(upper != NULL, "default init must reserve memory");

	if (!check_init(upper, 4, 0, 1 << 20, LLFREE_INIT_VOLATILE, true)) {
		success = false;
	}

	llfree_drop(upper);
	return success;
}

declare_test(llfree_alloc_s)
{
	int success = true;
	const size_t SIZE = (1ul << 30);
	void *memory = llfree_ext_alloc(LLFREE_ALIGN, SIZE);
	assert(memory != NULL);
	llfree_t *upper = llfree_ext_alloc(LLFREE_CACHE_SIZE, sizeof(llfree_t));
	check_m(upper != NULL, "default init must reserve memory");

	llfree_info("Init");
	llfree_result_t res = llfree_init(upper, 1,
					  (uint64_t)memory / LLFREE_FRAME_SIZE,
					  SIZE / LLFREE_FRAME_SIZE,
					  LLFREE_INIT_OVERWRITE, true);
	check(llfree_ok(res));

	llfree_info("Alloc");
	size_t n = 128;
	for (size_t i = 0; i < n; i++) {
		check(llfree_ok(llfree_get(upper, 0, 0)));
		check(llfree_ok(
			llfree_get(upper, 0, LLFREE_HUGE_ORDER)));
	}

	check_equal(n + (n << LLFREE_HUGE_ORDER),
		    llfree_frames(upper) - llfree_free_frames(upper));
	check_equal(n + (n << LLFREE_HUGE_ORDER),
		    upper->lower.frames - lower_free_frames(&upper->lower));

	check_equal(llfree_free_frames(upper),
		    lower_free_frames(&upper->lower));

	for (size_t i = 0; i < upper->trees_len; i++) {
		size_t free = 0;
		for (size_t j = 0; j < LLFREE_TREE_CHILDREN; j++) {
			size_t idx = i * LLFREE_TREE_CHILDREN + j;
			child_t child = atom_load(&upper->lower.children[idx]);
			free += child.free;
		}
		tree_t tree = atom_load(&upper->trees[i]);
		if (!tree.reserved)
			check_equal_m(free, tree.free, "tree %lu", i);
	}

	llfree_drop(upper);
	return success;
}

declare_test(llfree_general_function)
{
	bool success = true;

	llfree_t *upper = llfree_ext_alloc(LLFREE_CACHE_SIZE, sizeof(llfree_t));
	llfree_result_t ret =
		llfree_init(upper, 4, 0, 132000, LLFREE_INIT_VOLATILE, true);
	check_m(llfree_ok(ret), "init is success");
	check(upper->trees_len == 9);
	check(upper->cores == 4);
	check(llfree_frames(upper) == 132000);
	check_m(llfree_free_frames(upper) == 132000,
		"right number of free frames");

	// check allignment
	check_m((uint64_t)upper->trees % LLFREE_CACHE_SIZE == 0,
		"Alignment of trees");
	for (unsigned i = 0; i < upper->cores; ++i) {
		check_equal_m((uint64_t)&upper->local[i] % LLFREE_CACHE_SIZE,
			      0ul, "Alignment of local");
	}

	llfree_info("Before get");

	llfree_result_t frame = llfree_get(upper, 0, 0);
	check_m(llfree_ok(frame), "reservation must be success");

	check(llfree_frames(upper) == 132000);
	check_m(llfree_free_frames(upper) == 131999,
		"right number of free frames");

	llfree_info("After get mit core 0\n");

	ret = llfree_put(upper, 0, frame.val, 0);

	return success;

	check_m(llfree_ok(ret), "successfully free");
	check_m(llfree_free_frames(upper) == 132000,
		"right number of free frames");

	last_free_t lf = atom_load(&upper->local->last_free);
	check_equal(lf.tree_idx, row_from_pfn(frame.val));

	// reserve all frames in first tree
	for (size_t i = 0; i < LLFREE_TREE_SIZE; ++i) {
		ret = llfree_get(upper, 0, 0);
		check(llfree_ok(ret));
	}

	// reserve first frame in new tree
	ret = llfree_get(upper, 0, 0);
	check(llfree_ok(ret));
	reserved_t reserved = atom_load(&upper->local[0].reserved);
	check_m(reserved.start_row == row_from_pfn(LLFREE_TREE_SIZE),
		"second tree must be allocated");

	uint64_t free_frames = llfree_free_frames(upper);
	// reserve and free a HugeFrame
	frame = llfree_get(upper, 0, LLFREE_HUGE_ORDER);
	check(llfree_ok(frame));
	check_equal(llfree_free_frames(upper), free_frames - LLFREE_CHILD_SIZE);
	check(llfree_ok(
		llfree_put(upper, 0, frame.val, LLFREE_HUGE_ORDER)));
	check_equal(llfree_free_frames(upper), free_frames);

	llfree_drop(upper);
	return success;
}

declare_test(llfree_put)
{
	bool success = true;
	llfree_t *upper = (llfree_t *)llfree_ext_alloc(LLFREE_CACHE_SIZE,
						       sizeof(llfree_t));
	llfree_result_t ret = llfree_init(upper, 4, 0, LLFREE_TREE_SIZE << 4,
					  LLFREE_INIT_VOLATILE, true);
	assert(llfree_ok(ret));

	int64_t reserved[LLFREE_TREE_SIZE + 5];

	// reserve more frames than one tree
	for (size_t i = 0; i < LLFREE_TREE_SIZE + 5; ++i) {
		ret = llfree_get(upper, 1, 0);
		check(llfree_ok(ret));
		reserved[i] = ret.val;
	}

	check_equal(tree_from_pfn(reserved[0]),
		    tree_from_pfn(reserved[LLFREE_TREE_SIZE - 1]));
	check(tree_from_pfn(reserved[0]) !=
	      tree_from_pfn(reserved[LLFREE_TREE_SIZE]));

	llfree_result_t ret2 = llfree_get(upper, 2, 0);
	check(llfree_ok(ret2));
	check_m(tree_from_pfn(ret2.val) !=
			tree_from_pfn(reserved[LLFREE_TREE_SIZE]),
		"second get must be in different tree");

	if (!success)
		llfree_print(upper);

	// free half the frames from old tree with core 2
	for (size_t i = 0; i < LLFREE_TREE_SIZE / 2; ++i) {
		ret = llfree_put(upper, 2, reserved[i], 0);
		check(llfree_ok(ret));
	}

	// core 2 must have now this first tree reserved
	reserved_t res = atom_load(&upper->local[2].reserved);
	check_equal(tree_from_row(res.start_row),
		    (uint64_t)tree_from_pfn(reserved[0]));
	if (!success)
		llfree_print(upper);

	return success;
}

declare_test(llfree_alloc_all)
{
	const uint64_t MEMORYSIZE = (8ul << 30); // 8GiB
	const uint64_t LENGTH = (MEMORYSIZE / LLFREE_FRAME_SIZE);
	const int CORES = 8;
	int success = true;
	llfree_t *upper = llfree_ext_alloc(LLFREE_CACHE_SIZE, sizeof(llfree_t));
	llfree_result_t ret = llfree_init(upper, CORES, 1024, LENGTH, 0, true);
	assert(llfree_ok(ret));

	int64_t *pfns = malloc(sizeof(int64_t) * LENGTH);
	assert(pfns != NULL);

	for (size_t i = 0; i < LENGTH; ++i) {
		llfree_result_t ret = llfree_get(upper, i % CORES, 0);
		check_m(llfree_ok(ret),
			"must be able to alloc the whole memory");
		if (!llfree_ok(ret)) {
			llfree_print(upper);
			break;
		}
		pfns[i] = ret.val;
	}
	ret = llfree_get(upper, 0, 0);
	check(ret.val == LLFREE_ERR_MEMORY);

	check_equal(llfree_free_frames(upper), 0ul);
	check_equal(llfree_free_frames(upper),
		    lower_free_frames(&upper->lower));

	return success;
}

struct arg {
	int core;
	int order;
	size_t amount;
	size_t allocations;
};
struct ret {
	int64_t *pfns;
	size_t sp;
	size_t amount_ENOMEM;
};

llfree_t *upper;

static int64_t contains(int64_t *sorted_list, size_t len, int64_t item)
{
	if (item < sorted_list[0] || item > sorted_list[len - 1])
		return -1;

	for (size_t i = 0; i < len; ++i) {
		if (sorted_list[i] == item)
			return i;
		if (sorted_list[i] > item)
			return -1;
	}
	return -1;
}

static int comp(const void *a, const void *b)
{
	int64_t *x = (int64_t *)a;
	int64_t *y = (int64_t *)b;

	return *x - *y;
}

static void *alloc_frames(void *arg)
{
	struct arg *args = arg;

	struct ret *ret = malloc(sizeof(struct ret));
	assert(ret != NULL);

	ret->pfns = malloc(args->amount * sizeof(int64_t));
	assert(ret->pfns != NULL);

	srand(args->core);
	for (size_t i = 0; i < args->allocations; ++i) {
		// if full or in 1/3 times free a random reserved frame;
		if (ret->sp == args->amount ||
		    (ret->sp > 0 && rand() % 8 > 4)) {
			size_t random_idx = rand() % ret->sp;
			llfree_result_t res = llfree_put(upper, args->core,
							 ret->pfns[random_idx],
							 args->order);
			assert(llfree_ok(res));
			--ret->sp;
			ret->pfns[random_idx] = ret->pfns[ret->sp];
			--i;
		} else {
			llfree_result_t res =
				llfree_get(upper, args->core, args->order);
			assert(llfree_ok(res));
			ret->pfns[ret->sp] = res.val;
			if (ret->pfns[ret->sp] == LLFREE_ERR_MEMORY)
				++ret->amount_ENOMEM;
			else
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

	int success = true;

	const uint64_t LENGTH = 16 << 18;
	char *memory =
		llfree_ext_alloc(LLFREE_ALIGN, LENGTH << LLFREE_FRAME_BITS);
	assert(memory != NULL);

	upper = llfree_ext_alloc(LLFREE_CACHE_SIZE, sizeof(llfree_t));
	assert(llfree_ok(
		llfree_init(upper, CORES, (uint64_t)memory / LLFREE_FRAME_SIZE,
			    LENGTH, LLFREE_INIT_OVERWRITE, true)));
	pthread_t threads[CORES];
	struct arg args[CORES];
	for (int i = 0; i < CORES; ++i) {
		args[i] = (struct arg){ i, 0, (LLFREE_TREE_SIZE + 500), 40000 };
		assert(pthread_create(&threads[i], NULL, alloc_frames,
				      &args[i]) == 0);
	}

	struct ret *rets[CORES];
	for (int i = 0; i < CORES; ++i) {
		assert(pthread_join(threads[i], (void **)&rets[i]) == 0);
		assert(rets[i] != NULL);
	}

	size_t still_reserved = 0;
	size_t err = 0;
	for (int i = 0; i < CORES; ++i) {
		still_reserved += rets[i]->sp;
		err += rets[i]->amount_ENOMEM;
	}
	// now all threads are terminated
	check_equal(llfree_frames(upper) - llfree_free_frames(upper),
		    still_reserved);
	check_equal(llfree_free_frames(upper),
		    lower_free_frames(&upper->lower));

	// duplicate check
	for (size_t core = 0; core < CORES; ++core) {
		for (size_t i = core + 1; i < CORES; ++i) {
			for (size_t idx = 0; idx < rets[core]->sp; ++idx) {
				int64_t frame = rets[core]->pfns[idx];
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
		llfree_print(upper);
		printf("%zu times LLFREE_ERR_MEMORY was returned\n", err);
	}
	for (size_t i = 0; i < CORES; ++i) {
		free(rets[i]->pfns);
		free(rets[i]);
	}
	llfree_drop(upper);
	llfree_ext_free(LLFREE_ALIGN, LENGTH << LLFREE_FRAME_BITS, memory);
	return success;
}

struct par_args {
	llfree_t *upper;
	uint64_t **frames;
	size_t core;
};

#undef CORES
#define CORES 4
#undef OFFSET
#define OFFSET 0x1000000

static void *parallel_alloc_all(void *input)
{
	struct par_args *args = input;

	size_t total = llfree_frames(args->upper);
	*(args->frames) =
		llfree_ext_alloc(LLFREE_FRAME_SIZE, total * sizeof(uint64_t));

	size_t i = 0;
	for (;; i++) {
		assert(i < total);

		llfree_result_t ret = llfree_get(args->upper, args->core, 0);
		if (!llfree_ok(ret)) {
			assert(ret.val == LLFREE_ERR_MEMORY);
			break;
		}
		assert(ret.val >= OFFSET);
		assert((uint64_t)ret.val < OFFSET + total);

		(*(args->frames))[i] = ret.val;
	}
	llfree_info("finish %zu (%zu)", args->core, i);
	return (void *)i;
}

declare_test(llfree_parallel_alloc_all)
{
	bool success = true;
	const uint64_t LENGTH = 8 << 18; // 8G

	upper = llfree_ext_alloc(LLFREE_CACHE_SIZE, sizeof(llfree_t));
	assert(llfree_ok(llfree_init(upper, CORES, OFFSET, LENGTH,
					    LLFREE_INIT_VOLATILE, true)));

	uint64_t *frames[CORES] = { NULL };
	struct par_args args[CORES];

	pthread_t threads[CORES];
	for (size_t i = 0; i < CORES; i++) {
		llfree_info("spawn %zu", i);
		args[i] = (struct par_args){ upper, &frames[i], i };
		assert(pthread_create(&threads[i], NULL, parallel_alloc_all,
				      &args[i]) == 0);
	}

	size_t counts[CORES] = { 0 };
	uint64_t *all_frames = llfree_ext_alloc(
		LLFREE_FRAME_SIZE, llfree_frames(upper) * sizeof(uint64_t));

	size_t total = 0;
	for (size_t i = 0; i < CORES; i++) {
		llfree_info("wait for %zu", i);
		assert(pthread_join(threads[i], (void **)&counts[i]) == 0);

		// collect allocated pages
		memcpy(&all_frames[total], frames[i],
		       counts[i] * sizeof(uint64_t));
		llfree_ext_free(sizeof(uint64_t), llfree_frames(upper),
				frames[i]);
		total += counts[i];
	}

	check_equal(llfree_frames(upper), total);
	check_equal(llfree_free_frames(upper),
		    lower_free_frames(&upper->lower));

	// check for duplicates
	qsort(all_frames, total, sizeof(int64_t), comp);
	for (size_t i = 1; i < total; i++) {
		check_m(all_frames[i] >= OFFSET &&
				all_frames[i] < (OFFSET + llfree_frames(upper)),
			"check %" PRIx64, all_frames[i]);
		check_m(all_frames[i - 1] != all_frames[i],
			"dup %" PRIx64 " at %zu", all_frames[i], i);
	}

	return success;
}

declare_test(llfree_get_huge)
{
	bool success = true;

	llfree_t llfree;
	llfree_result_t res = llfree_init(&llfree, 1, 0, LLFREE_TREE_SIZE,
					  LLFREE_INIT_VOLATILE, true);
	check(llfree_ok(res));

	llfree_info("get");
	res = llfree_get(&llfree, 0, LLFREE_HUGE_ORDER);
	check(llfree_ok(res));

	llfree_info("get");
	res = llfree_get(&llfree, 0, LLFREE_HUGE_ORDER);
	check(llfree_ok(res));

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
		llfree_result_t res = llfree_get(shared->llfree, data->core, 0);
		assert(llfree_ok(res));
		frames[i] = res.val;
	}

	llfree_info("sync1 on %zu", data->core);
	atomic_fetch_add(&shared->sync1, 1);
	while (atom_load(&shared->sync1) < shared->cores) {
		spin_wait();
	}

	for (size_t i = 0; i < shared->frames_len; i++) {
		llfree_result_t res =
			llfree_put(shared->llfree, data->core, frames[i], 0);
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

	llfree_t llfree;
	llfree_result_t res = llfree_init(&llfree, CORES, 0, FRAMES,
					  LLFREE_INIT_VOLATILE, true);
	check(llfree_ok(res));

	llfree_info("get");

	struct llfree_less_mem shared = { .sync0 = 0,
					  .sync1 = 0,
					  .llfree = &llfree,
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

	check_equal(llfree_free_frames(&llfree), FRAMES);

	return success;
}
