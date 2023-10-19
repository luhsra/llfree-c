#include "check.h"
#include "llc.h"
#include "local.h"
#include "lower.h"
#include "pthread.h"
#include "utils.h"

#include <stddef.h>
#include <stdlib.h>
#include <memory.h>

void print_trees(llc_t *self)
{
	printf("trees:\ti\tflag\tcounter\n");
	for (size_t i = 0; i < self->trees_len; ++i) {
		tree_t _unused tree = atom_load(&self->trees[i]);
		info("\t%lu\t%d\t%X\n", i, tree.reserved, tree.free);
	}
}

bool check_init(llc_t *upper, size_t cores, uint64_t start_frame_adr,
		size_t len, uint8_t init, uint8_t free_all)
{
	bool success = true;
	result_t ret = llc_init(upper, cores, start_frame_adr / PAGESIZE, len,
				init, free_all);
	size_t num_trees = div_ceil(len, TREESIZE);
	size_t num_childs = div_ceil(len, CHILDSIZE);
	check_equal((uint64_t)upper->trees % CACHE_SIZE, 0ull);

	check_m(result_ok(ret), "init is success");
	check_equal(upper->trees_len, num_trees);
	check_equal(upper->lower.childs_len, num_childs);
	check_equal(upper->lower.offset, start_frame_adr);
	check_equal(upper->cores, (cores > num_trees ? num_trees : cores));
	check_equal(llc_frames(upper), len);
	check_equal(llc_free_frames(upper), free_all ? len : 0);
	reserved_t reserved = atom_load(&upper->local[0].reserved);
	check(!reserved.present);

	return success;
}

declare_test(llc_init)
{
	int success = true;

	llc_t *upper = llc_ext_alloc(CACHE_SIZE, sizeof(llc_t));
	check_m(upper != NULL, "default init must reserve memory");

	if (!check_init(upper, 4, 0, 1 << 20, VOLATILE, true)) {
		success = false;
	}

	llc_drop(upper);
	return success;
}

declare_test(llc_general_function)
{
	bool success = true;

	llc_t *upper = llc_ext_alloc(CACHE_SIZE, sizeof(llc_t));
	result_t ret = llc_init(upper, 4, 0, 132000, VOLATILE, true);
	check_m(result_ok(ret), "init is success");
	check(upper->trees_len == 9);
	check(upper->cores == 4);
	check(llc_frames(upper) == 132000);
	check_m(llc_free_frames(upper) == 132000,
		"right number of free frames");

	// check allignment
	check_m((uint64_t)upper->trees % CACHE_SIZE == 0, "Alignment of trees");
	for (unsigned i = 0; i < upper->cores; ++i) {
		check_equal_m((uint64_t)&upper->local[i] % CACHE_SIZE, 0ul,
			      "Alignment of local");
	}

	info("Before get");

	result_t frame = llc_get(upper, 0, 0);
	check_m(result_ok(frame), "reservation must be success");

	check(llc_frames(upper) == 132000);
	check_m(llc_free_frames(upper) == 131999,
		"right number of free frames");

	info("After get mit core 0\n");

	ret = llc_put(upper, 0, frame.val, 0);

	return success;

	check_m(result_ok(ret), "successfully free");
	check_m(llc_free_frames(upper) == 132000,
		"right number of free frames");

	last_free_t lf = atom_load(&upper->local->last_free);
	check_equal(lf.tree_idx, row_from_pfn(frame.val));

	// reserve all frames in first tree
	for (size_t i = 0; i < TREESIZE; ++i) {
		ret = llc_get(upper, 0, 0);
		check(result_ok(ret));
	}

	// reserve first frame in new tree
	ret = llc_get(upper, 0, 0);
	check(result_ok(ret));
	reserved_t reserved = atom_load(&upper->local[0].reserved);
	check_m(reserved.start_row == row_from_pfn(TREESIZE),
		"second tree must be allocated");

	uint64_t free_frames = llc_free_frames(upper);
	// reserve and free a HugeFrame
	frame = llc_get(upper, 0, HP_ORDER);
	check(result_ok(frame));
	check_equal(llc_free_frames(upper), free_frames - CHILD_SIZE);
	check(result_ok(llc_put(upper, 0, frame.val, HP_ORDER)));
	check_equal(llc_free_frames(upper), free_frames);

	llc_drop(upper);
	return success;
}

declare_test(llc_put)
{
	bool success = true;
	llc_t *upper = (llc_t *)llc_ext_alloc(CACHE_SIZE, sizeof(llc_t));
	result_t ret = llc_init(upper, 4, 0, TREESIZE << 4, VOLATILE, true);
	assert(result_ok(ret));

	int64_t reserved[TREESIZE + 5];

	// reserve more frames than one tree
	for (size_t i = 0; i < TREESIZE + 5; ++i) {
		ret = llc_get(upper, 1, 0);
		check(result_ok(ret));
		reserved[i] = ret.val;
	}

	check_equal(tree_from_pfn(reserved[0]),
		    tree_from_pfn(reserved[TREESIZE - 1]));
	check(tree_from_pfn(reserved[0]) != tree_from_pfn(reserved[TREESIZE]));

	result_t ret2 = llc_get(upper, 2, 0);
	check(result_ok(ret2));
	check_m(tree_from_pfn(ret2.val) != tree_from_pfn(reserved[TREESIZE]),
		"second get must be in different tree");

	if (!success)
		llc_print(upper);

	// free half the frames from old tree with core 2
	for (size_t i = 0; i < TREESIZE / 2; ++i) {
		ret = llc_put(upper, 2, reserved[i], 0);
		check(result_ok(ret));
	}

	// core 2 must have now this first tree reserved
	reserved_t res = atom_load(&upper->local[2].reserved);
	check_equal(tree_from_row(res.start_row),
		    (uint64_t)tree_from_pfn(reserved[0]));
	if (!success)
		llc_print(upper);

	return success;
}

declare_test(llc_alloc_all)
{
	const uint64_t MEMORYSIZE = (8ul << 30); // 8GiB
	const uint64_t LENGTH = (MEMORYSIZE / FRAME_SIZE);
	const int CORES = 8;
	int success = true;
	llc_t *upper = llc_ext_alloc(CACHE_SIZE, sizeof(llc_t));
	result_t ret = llc_init(upper, CORES, 1024, LENGTH, 0, true);
	assert(result_ok(ret));

	int64_t *pfns = malloc(sizeof(int64_t) * LENGTH);
	assert(pfns != NULL);

	for (size_t i = 0; i < LENGTH; ++i) {
		result_t ret = llc_get(upper, i % CORES, 0);
		check_m(result_ok(ret),
			"must be able to alloc the whole memory");
		if (!result_ok(ret)) {
			llc_print(upper);
			break;
		}
		pfns[i] = ret.val;
	}
	ret = llc_get(upper, 0, 0);
	check(ret.val == ERR_MEMORY);

	check_equal(llc_free_frames(upper), 0ul);

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

llc_t *upper;

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
			result_t res = llc_put(upper, args->core,
					       ret->pfns[random_idx],
					       args->order);
			assert(result_ok(res));
			--ret->sp;
			ret->pfns[random_idx] = ret->pfns[ret->sp];
			--i;
		} else {
			result_t res = llc_get(upper, args->core, args->order);
			assert(result_ok(res));
			ret->pfns[ret->sp] = res.val;
			if (ret->pfns[ret->sp] == ERR_MEMORY)
				++ret->amount_ENOMEM;
			else
				++ret->sp;
		}
	}
	qsort(ret->pfns, ret->sp, sizeof(int64_t), comp);
	return ret;
}

declare_test(llc_parallel_alloc)
{
#undef CORES
#define CORES 4

	int success = true;

	const uint64_t LENGTH = 16 << 18;
	char *memory = aligned_alloc(1 << HP_ORDER, LENGTH << FRAME_BITS);
	assert(memory != NULL);

	upper = llc_ext_alloc(CACHE_SIZE, sizeof(llc_t));
	assert(result_ok(llc_init(upper, CORES, (uint64_t)memory / PAGESIZE,
				  LENGTH, OVERWRITE, true)));
	pthread_t threads[CORES];
	struct arg args[CORES];
	for (int i = 0; i < CORES; ++i) {
		args[i] = (struct arg){ i, 0, (TREESIZE + 500), 40000 };
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
	check_equal(llc_frames(upper) - llc_free_frames(upper), still_reserved);

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
				printf("\tFound duplicate reserved Frame\n both core %lu and %lu "
				       "reserved frame %ld in tree %lu\n",
				       core, i, frame, tree_from_pfn(frame));
				// leave all loops
				goto end;
			}
		}
	}
end:

	if (!success) {
		llc_print(upper);
		printf("%lu times ERR_MEMORY was returned\n", err);
	}
	for (size_t i = 0; i < CORES; ++i) {
		free(rets[i]->pfns);
		free(rets[i]);
	}
	llc_drop(upper);
	free(memory);
	return success;
}

struct par_args {
	llc_t *upper;
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

	size_t total = llc_frames(args->upper);
	*(args->frames) = llc_ext_alloc(PAGESIZE, total * sizeof(uint64_t));

	size_t i = 0;
	for (;; i++) {
		assert(i < total);

		result_t ret = llc_get(args->upper, args->core, 0);
		if (!result_ok(ret)) {
			assert(ret.val == ERR_MEMORY);
			break;
		}
		assert(ret.val >= OFFSET);
		assert((uint64_t)ret.val < OFFSET + total);

		(*(args->frames))[i] = ret.val;
	}
	info("finish %lu (%lu)", args->core, i);
	return (void *)i;
}

declare_test(llc_parallel_alloc_all)
{
	bool success = true;
	const uint64_t LENGTH = 8 << 18; // 8G

	upper = llc_ext_alloc(CACHE_SIZE, sizeof(llc_t));
	assert(result_ok(
		llc_init(upper, CORES, OFFSET, LENGTH, VOLATILE, true)));

	uint64_t *frames[CORES] = { NULL };
	struct par_args args[CORES];

	pthread_t threads[CORES];
	for (size_t i = 0; i < CORES; i++) {
		info("spawn %lu", i);
		args[i] = (struct par_args){ upper, &frames[i], i };
		assert(pthread_create(&threads[i], NULL, parallel_alloc_all,
				      &args[i]) == 0);
	}

	size_t counts[CORES] = { 0 };
	uint64_t *all_frames =
		llc_ext_alloc(PAGESIZE, llc_frames(upper) * sizeof(uint64_t));

	size_t total = 0;
	for (size_t i = 0; i < CORES; i++) {
		info("wait for %lu", i);
		assert(pthread_join(threads[i], (void **)&counts[i]) == 0);

		// collect allocated pages
		memcpy(&all_frames[total], frames[i],
		       counts[i] * sizeof(uint64_t));
		llc_ext_free(sizeof(uint64_t), llc_frames(upper), frames[i]);
		total += counts[i];
	}

	check_equal(llc_frames(upper), total);

	// check for duplicates
	qsort(all_frames, total, sizeof(int64_t), comp);
	for (size_t i = 1; i < total; i++) {
		check_m(all_frames[i] >= OFFSET &&
				all_frames[i] < (OFFSET + llc_frames(upper)),
			"check %lx", all_frames[i]);
		check_m(all_frames[i - 1] != all_frames[i], "dup %lx at %lu",
			all_frames[i], i);
	}

	return success;
}
