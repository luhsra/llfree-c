#include "llc_tests.h"

#include "check.h"
#include "llc.h"
#include "local.h"
#include "lower.h"
#include "pthread.h"
#include "utils.h"

#include <stdlib.h>

void print_trees(upper_t *self)
{
	printf("TREES:\nIDX\tFlag\tCounter\n");
	for (size_t i = 0; i < self->trees_len; ++i) {
		tree_t _unused tree = atom_load(&self->trees[i]);
		info("%lu\t%d\t%X\n", i, tree.flag, tree.counter);
	}
}

bool general_function_test()
{
	bool success = true;

	upper_t *upper = (upper_t *)llc_default();
	result_t ret = llc_init(upper, 4, 0, 132000, 0, true);
	check(result_ok(ret), "init is success");
	check(upper->trees_len == 9, "");
	check(upper->cores == 4, "");
	check(llc_frames(upper) == 132000, "");
	check(llc_free_frames(upper) == 132000, "right number of free frames");

	//check allignment
	check((uint64_t)upper->trees % CACHESIZE == 0, "Alignment of trees");
	for (unsigned i = 0; i < upper->cores; ++i) {
		check_equal_m((uint64_t)&upper->local[i] % CACHESIZE, 0ul,
			      "Alignment of local");
	}

	info("Before get");

	result_t frame = llc_get(upper, 0, 0);
	check(result_ok(frame), "reservation must be success");

	check(llc_frames(upper) == 132000, "");
	check(llc_free_frames(upper) == 131999, "right number of free frames");

	info("After get mit core 0\n");

	ret = llc_put(upper, 0, frame.val, 0);

	check(result_ok(ret), "successfully free");
	check(llc_free_frames(upper) == 132000, "right number of free frames");

	last_free_t lf = atom_load(&upper->local->last_free);
	check_equal(lf.last_tree, atomic_from_pfn(frame.val));

	// reserve all frames in first tree
	for (int i = 0; i < TREESIZE; ++i) {
		ret = llc_get(upper, 0, 0);
		check(result_ok(ret), "");
	}

	check(result_ok(ret), "");

	// reserve first frame in new tree
	ret = llc_get(upper, 0, 0);
	check(result_ok(ret), "");
	reserved_t reserved = atom_load(&upper->local[0].reserved);
	check(reserved.preferred_index = atomic_from_pfn(TREESIZE),
	      "second tree must be allocated");

	uint64_t free_frames = llc_free_frames(upper);
	// reserve and free a HugeFrame
	frame = llc_get(upper, 0, HP_ORDER);
	check(result_ok(frame), "");
	check_equal(llc_free_frames(upper), free_frames - FIELDSIZE);
	check(result_ok(llc_put(upper, 0, frame.val, HP_ORDER)), "");
	check_equal(llc_free_frames(upper), free_frames);

	llc_drop(upper);
	return success;
}

bool check_init(upper_t *upper, size_t cores, uint64_t start_frame_adr,
		size_t len, uint8_t init, uint8_t free_all)
{
	bool success = true;
	result_t ret =
		llc_init(upper, cores, start_frame_adr, len, init, free_all);
	size_t num_trees = div_ceil(len, TREESIZE);
	size_t num_childs = div_ceil(len, CHILDSIZE);
	check_equal((uint64_t)upper->trees % CACHESIZE, 0ull);

	check(result_ok(ret), "init is success");
	check_equal(upper->trees_len, num_trees);
	check_equal(upper->lower.childs_len, num_childs);
	check_equal(upper->lower.start_frame_adr, start_frame_adr);
	check_equal(upper->cores, (cores > num_trees ? num_trees : cores));
	check_equal(llc_frames(upper), len);
	check_equal(llc_free_frames(upper), free_all ? len : 0);
	reserved_t reserved = atom_load(&upper->local[0].reserved);
	check(!reserved.present, "");

	return success;
}

bool init_llc_test()
{
	int success = true;

	upper_t *upper = llc_default();
	check(upper != NULL, "default init must reserve memory");

	if (!check_init(upper, 4, 0, 1 << 20, VOLATILE, true)) {
		success = false;
	}

	llc_drop(upper);
	return success;
}

bool test_put()
{
	bool success = true;
	upper_t *upper = (upper_t *)llc_default();
	result_t ret = llc_init(upper, 4, 0, TREESIZE << 4, VOLATILE, true);
	assert(result_ok(ret));

	int64_t reservedByCore1[TREESIZE + 5];

	// reserve more frames than one tree
	for (int i = 0; i < TREESIZE + 5; ++i) {
		ret = llc_get(upper, 1, 0);
		check(result_ok(ret), "");
		reservedByCore1[i] = ret.val;
	}

	check_equal(tree_from_pfn(reservedByCore1[0]),
		    tree_from_pfn(reservedByCore1[TREESIZE - 1]));
	check(tree_from_pfn(reservedByCore1[0]) !=
		      tree_from_pfn(reservedByCore1[TREESIZE]),
	      "");

	result_t ret2 = llc_get(upper, 2, 0);
	assert(result_ok(ret2));
	check(tree_from_pfn(ret2.val) !=
		      tree_from_pfn(reservedByCore1[TREESIZE]),
	      "second get must be in different tree");

	if (!success)
		llc_print(upper);

	// free half the frames from old tree with core 2
	for (int i = 0; i < TREESIZE / 2; ++i) {
		ret = llc_put(upper, 2, reservedByCore1[i], 0);
		check(result_ok(ret), "");
	}

	// core 2 must have now this first tree reserved
	reserved_t reserved = atom_load(&upper->local[2].reserved);
	check_equal(tree_from_atomic(reserved.preferred_index),
		    (uint64_t)tree_from_pfn(reservedByCore1[0]));
	if (!success)
		llc_print(upper);

	return success;
}

bool llc_allocAll()
{
	const uint64_t MEMORYSIZE = (1ul << 30); // 8GiB
	const uint64_t LENGTH = (MEMORYSIZE / FRAME_SIZE);
	const int CORES = 8;
	int success = true;
	upper_t *upper = llc_default();
	result_t ret = llc_init(upper, CORES, 1024, LENGTH, 0, true);
	assert(result_ok(ret));

	int64_t *pfns = malloc(sizeof(int64_t) * LENGTH);
	assert(pfns != NULL);

	for (size_t i = 0; i < LENGTH; ++i) {
		result_t ret = llc_get(upper, i % CORES, 0);
		check(result_ok(ret), "must be able to alloc the whole memory");
		if (!result_ok(ret)) {
			llc_print(upper);
			break;
		}
		pfns[i] = ret.val;
	}
	check_equal(llc_free_frames(upper), 0ul);
	check_equal(lower_allocated_frames(&upper->lower), llc_frames(upper));

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

upper_t *upper;

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

static void *allocFrames(void *arg)
{
	struct arg *args = arg;

	struct ret *ret = malloc(sizeof(struct ret));
	assert(ret != NULL);

	ret->pfns = malloc(args->amount * sizeof(int64_t));
	assert(ret->pfns != NULL);

	srandom(args->core);
	for (size_t i = 0; i < args->allocations; ++i) {
		// if full or in 1/3 times free a random reserved frame;
		if (ret->sp == args->amount ||
		    (ret->sp > 0 && random() % 8 > 4)) {
			size_t random_idx = random() % ret->sp;
			assert(result_ok(llc_put(upper, args->core,
						 ret->pfns[random_idx],
						 args->order)));
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
	ret->pfns = realloc(ret->pfns, ret->sp * sizeof(int64_t));
	assert(ret->pfns != NULL);
	qsort(ret->pfns, ret->sp, sizeof(int64_t), comp);
	pthread_exit(ret);
	return NULL;
}
bool multithreaded_alloc()
{
	int success = true;

	const int CORES = 4;
	const uint64_t LENGTH = 16 << 18;
	char *memory = aligned_alloc(1 << HP_ORDER, LENGTH << 12);
	assert(memory != NULL);

	upper = llc_default();
	assert(result_ok(llc_init(upper, CORES, (uint64_t)memory, LENGTH,
				  OVERWRITE, true)));
	pthread_t threads[CORES];
	struct arg args[CORES];
	for (int i = 0; i < CORES; ++i) {
		args[i] = (struct arg){ i, 0, (TREESIZE + 500), 40000 };
		assert(pthread_create(&threads[i], NULL, allocFrames,
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
				if (same >= 0) {
					success = false;
					printf("\tFound duplicate reserved Frame\n both core %lu and %lu "
					       "reserved frame %lld in tree %llu\n",
					       core, i, frame,
					       tree_from_pfn(frame));
					// leave all loops
					goto end;
				}
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

int llc_tests(int *test_counter, int *fail_counter)
{
	run_test(init_llc_test);
	run_test(general_function_test);
	run_test(test_put);
	run_test(llc_allocAll);
	run_test(multithreaded_alloc);
	return 0;
}
