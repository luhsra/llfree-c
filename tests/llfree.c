#include "llfree_inner.h"

#include "check.h"
#include "llfree_platform.h"
#include "local.h"
#include "lower.h"

#include <memory.h>
#include <pthread.h>
#include <stdlib.h>


static llfree_t llfree_new(size_t cores, size_t frames, uint8_t init)
{
	llfree_t upper;
	llfree_meta_size_t m = llfree_metadata_size(cores, frames);
	llfree_meta_t meta = {
		.local = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.local),
		.trees = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.trees),
		.lower = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.lower),
	};
	llfree_result_t ll_unused ret =
		llfree_init(&upper, cores, frames, init, meta);
	assert(llfree_is_ok(ret));
	return upper;
}

static void llfree_drop(llfree_t *self)
{
	llfree_meta_size_t ms =
		llfree_metadata_size(self->cores, llfree_frames(self));
	llfree_meta_t m = llfree_metadata(self);
	llfree_ext_free(LLFREE_CACHE_SIZE, ms.local, m.local);
	llfree_ext_free(LLFREE_CACHE_SIZE, ms.trees, m.trees);
	llfree_ext_free(LLFREE_CACHE_SIZE, ms.lower, m.lower);
}
#define lldrop __attribute__((cleanup(llfree_drop)))

declare_test(llfree_init)
{
	bool success = true;

	lldrop llfree_t upper = llfree_new(4, 1 << 20, LLFREE_INIT_FREE);
	check(llfree_frames(&upper) == 1 << 20);
	check(llfree_free_frames(&upper) == 1 << 20);

	llfree_validate(&upper);

	return success;
}

declare_test(llfree_init_alloc)
{
	bool success = true;

	const size_t FRAMES = (1 << 30) / LLFREE_FRAME_SIZE;

	lldrop llfree_t upper = llfree_new(4, FRAMES, LLFREE_INIT_ALLOC);
	check(llfree_frames(&upper) == FRAMES);
	check(llfree_free_frames(&upper) == 0);

	for (size_t hp = 0; hp < (FRAMES >> LLFREE_HUGE_ORDER) - 1; hp++) {
		check(llfree_is_ok(llfree_put(&upper, 0,
					      hp << LLFREE_HUGE_ORDER,
					      llflags(LLFREE_HUGE_ORDER))));
	}
	for (size_t page = FRAMES - (1 << LLFREE_HUGE_ORDER); page < FRAMES;
	     page++) {
		check(llfree_is_ok(llfree_put(&upper, 0, page, llflags(0))));
	}
	check(llfree_free_frames(&upper) == FRAMES);

	llflags_t llf = llflags(0);
	llf.movable = true;
	check(llfree_is_ok(llfree_get(&upper, 0, llf)));

	check(llfree_is_ok(llfree_get(&upper, 0, llflags(0))));

	check(llfree_is_ok(llfree_get(&upper, 0, llf)));

	llfree_validate(&upper);

	return success;
}

declare_test(llfree_alloc_s)
{
	bool success = true;
	const size_t FRAMES = (1ul << 30) / LLFREE_FRAME_SIZE;

	llfree_info("Init");
	lldrop llfree_t upper = llfree_new(4, FRAMES, LLFREE_INIT_FREE);
	llfree_validate(&upper);

	llfree_info("Alloc");
	size_t n = 128;
	for (size_t i = 0; i < n; i++) {
		check(llfree_is_ok(llfree_get(&upper, 0, llflags(0))));
		check(llfree_is_ok(
			llfree_get(&upper, 0, llflags(LLFREE_HUGE_ORDER))));
	}

	check_equal("zu", n + (n << LLFREE_HUGE_ORDER),
		    llfree_frames(&upper) - llfree_free_frames(&upper));
	check_equal("zu", n + (n << LLFREE_HUGE_ORDER),
		    upper.lower.frames - lower_free_frames(&upper.lower));

	check_equal("zu", llfree_free_frames(&upper),
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
			check_equal_m("zu", free, (size_t)tree.free,
				      "tree %lu", i);
	}

	llfree_validate(&upper);

	return success;
}

declare_test(llfree_general_function)
{
	bool success = true;
	llfree_result_t ret;

	lldrop llfree_t upper = llfree_new(4, 132000, LLFREE_INIT_FREE);
	llfree_validate(&upper);
	check_equal("zu", upper.trees_len, div_ceil(132000, LLFREE_TREE_SIZE));
	check(upper.cores == 4);
	check(llfree_frames(&upper) == 132000);
	check_m(llfree_free_frames(&upper) == 132000,
		"right number of free frames");

	// check allignment
	check_m((uint64_t)upper.trees % LLFREE_CACHE_SIZE == 0,
		"Alignment of trees");
	for (unsigned i = 0; i < upper.cores; ++i) {
		check_equal_m("zu",
			      (uint64_t)&upper.local[i] % LLFREE_CACHE_SIZE,
			      0ul, "Alignment of local");
	}

	llfree_info("Before get");

	llfree_result_t frame = llfree_get(&upper, 0, llflags(0));
	check_m(llfree_is_ok(frame), "reservation must be success");
	llfree_validate(&upper);

	check(llfree_frames(&upper) == 132000);
	check_m(llfree_free_frames(&upper) == 131999,
		"right number of free frames");

	llfree_info("After get mit core 0\n");

	ret = llfree_put(&upper, 0, frame.frame, llflags(0));
	llfree_validate(&upper);

	check_m(llfree_is_ok(ret), "successfully free");
	check_m(llfree_free_frames(&upper) == 132000,
		"right number of free frames");

	if (LLFREE_ENABLE_FREE_RESERVE) {
		local_history_t last = atom_load(&upper.local[0].last);
		check_equal("zu", last.idx, tree_from_frame(frame.frame));
	}

	// reserve all frames in first tree
	for (size_t i = 0; i < LLFREE_TREE_SIZE; ++i) {
		ret = llfree_get(&upper, 0, llflags(0));
		check(llfree_is_ok(ret));
	}

	llfree_validate(&upper);

	// reserve first frame in new tree
	ret = llfree_get(&upper, 0, llflags(0));
	check(llfree_is_ok(ret));
	check_m(tree_from_frame(frame.frame) != tree_from_frame(ret.frame),
		"second tree must be allocated");

	uint64_t free_frames = llfree_free_frames(&upper);
	// reserve and free a HugeFrame
	frame = llfree_get(&upper, 0, llflags(LLFREE_HUGE_ORDER));
	check(llfree_is_ok(frame));
	check_equal("zu", llfree_free_frames(&upper),
		    free_frames - LLFREE_CHILD_SIZE);
	check(llfree_is_ok(llfree_put(&upper, 0, frame.frame,
				      llflags(LLFREE_HUGE_ORDER))));
	check_equal("zu", llfree_free_frames(&upper), free_frames);

	llfree_validate(&upper);

	return success;
}

declare_test(llfree_put)
{
	bool success = true;
	llfree_result_t ret;

	lldrop llfree_t upper =
		llfree_new(4, LLFREE_TREE_SIZE << 4, LLFREE_INIT_FREE);
	llfree_validate(&upper);
	uint64_t reserved[LLFREE_TREE_SIZE + 5];

	// reserve more frames than one tree
	for (size_t i = 0; i < LLFREE_TREE_SIZE + 5; ++i) {
		ret = llfree_get(&upper, 1, llflags(0));
		check(llfree_is_ok(ret));
		reserved[i] = ret.frame;
	}

	check_equal("zu", tree_from_frame(reserved[0]),
		    tree_from_frame(reserved[LLFREE_TREE_SIZE - 1]));
	check(tree_from_frame(reserved[0]) !=
	      tree_from_frame(reserved[LLFREE_TREE_SIZE]));

	llfree_result_t ret2 = llfree_get(&upper, 2, llflags(0));
	check(llfree_is_ok(ret2));
	check_m(tree_from_frame(ret2.frame) !=
			tree_from_frame(reserved[LLFREE_TREE_SIZE]),
		"second get must be in different tree");

	if (!success)
		llfree_print(&upper);

	llfree_info("free");

	// free half the frames from old tree with core 2
	for (size_t i = 0; i < LLFREE_TREE_SIZE / 2; ++i) {
		ret = llfree_put(&upper, 2, reserved[i], llflags(0));
		check(llfree_is_ok(ret));
	}

	// core 2 must have now this first tree reserved
	if (LLFREE_ENABLE_FREE_RESERVE) {
		reserved_t res = upper.local[2].reserved[0];
		check_equal("zu", tree_from_row(res.start_row),
			    (uint64_t)tree_from_frame(reserved[0]));
	}
	if (!success)
		llfree_print(&upper);
	llfree_validate(&upper);

	return success;
}

declare_test(llfree_alloc_all)
{
	bool success = true;
	llfree_result_t ret;

	const int CORES = 8;
	const uint64_t LENGTH = ((8ul << 30) / LLFREE_FRAME_SIZE); // 8GiB
	lldrop llfree_t upper = llfree_new(CORES, LENGTH, LLFREE_INIT_FREE);
	llfree_validate(&upper);

	int64_t *frames = malloc(sizeof(int64_t) * LENGTH);
	assert(frames != NULL);

	for (size_t i = 0; i < LENGTH; ++i) {
		llfree_result_t ret = llfree_get(&upper, i % CORES, llflags(0));
		check_m(llfree_is_ok(ret),
			"must be able to alloc the whole memory");
		if (!llfree_is_ok(ret)) {
			llfree_print(&upper);
			break;
		}
		frames[i] = ret.frame;
	}
	ret = llfree_get(&upper, 0, llflags(0));
	check(ret.error == LLFREE_ERR_MEMORY);

	check_equal("zu", llfree_free_frames(&upper), 0ul);
	check_equal("zu", llfree_free_frames(&upper),
		    lower_free_frames(&upper.lower));
	llfree_validate(&upper);

	return success;
}

declare_test(llfree_alloc_all_kinds)
{
	bool success = true;
	llfree_result_t ret;

	const int CORES = 1;
	const uint64_t LENGTH = ((8ul << 30) / LLFREE_FRAME_SIZE); // 8GiB
	llfree_t upper = llfree_new(CORES, LENGTH, LLFREE_INIT_FREE);
	llfree_validate(&upper);

	int64_t *frames = malloc(sizeof(int64_t) * LENGTH);
	assert(frames != NULL);

	for (size_t i = 0; i < LENGTH; ++i) {
		llflags_t flags = llflags(0);
		flags.movable = i % 3 == 0;
		llfree_result_t ret = llfree_get(&upper, i % CORES, flags);
		if (!llfree_is_ok(ret)) {
			llfree_print(&upper);
			llfree_warn("err %u", ret.error);
			check_m(false, "must be able to alloc the whole memory");
			break;
		}
		frames[i] = ret.frame;
	}
	ret = llfree_get(&upper, 0, llflags(0));
	check(ret.error == LLFREE_ERR_MEMORY);

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
	uint64_t *frames;
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

	ret->frames = malloc(args->amount * sizeof(int64_t));
	assert(ret->frames != NULL);

	srand((unsigned int)args->core);
	for (size_t i = 0; i < args->allocations; ++i) {
		// if full or in 1/3 times free a random reserved frame;
		if (ret->sp == args->amount ||
		    (ret->sp > 0 && rand() % 8 > 4)) {
			size_t random_idx = (size_t)rand() % ret->sp;
			llfree_result_t res = llfree_put(
				args->upper, args->core,
				ret->frames[random_idx], llflags(args->order));
			assert(llfree_is_ok(res));
			--ret->sp;
			ret->frames[random_idx] = ret->frames[ret->sp];
			--i;
		} else {
			llfree_result_t res = llfree_get(
				args->upper, args->core, llflags(args->order));
			assert(llfree_is_ok(res));
			ret->frames[ret->sp] = res.frame;
			++ret->sp;
		}
	}
	qsort(ret->frames, ret->sp, sizeof(int64_t), comp);
	return ret;
}

declare_test(llfree_parallel_alloc)
{
#undef CORES
#define CORES 4lu

	bool success = true;

	const uint64_t LENGTH = 16 << 18;
	lldrop llfree_t upper = llfree_new(CORES, LENGTH, LLFREE_INIT_FREE);
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
	for (size_t i = 0; i < CORES; ++i) {
		assert(pthread_join(threads[i], (void **)&rets[i]) == 0);
		assert(rets[i] != NULL);
	}

	size_t still_reserved = 0;
	for (size_t i = 0; i < CORES; ++i) {
		still_reserved += rets[i]->sp;
	}
	// now all threads are terminated
	check_equal("zu", llfree_frames(&upper) - llfree_free_frames(&upper),
		    still_reserved);
	check_equal("zu", llfree_free_frames(&upper),
		    lower_free_frames(&upper.lower));
	llfree_validate(&upper);

	// duplicate check
	for (size_t core = 0; core < CORES; ++core) {
		for (size_t i = core + 1; i < CORES; ++i) {
			for (size_t idx = 0; idx < rets[core]->sp; ++idx) {
				uint64_t frame = rets[core]->frames[idx];
				int64_t same = contains(rets[i]->frames,
							rets[i]->sp, frame);
				if (same <= 0)
					continue;

				success = false;
				printf("\tFound duplicate reserved Frame\n both core %zu and %zu "
				       "reserved frame %" PRId64
				       " in tree %" PRId64 "\n",
				       core, i, frame, tree_from_frame(frame));
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
		free(rets[i]->frames);
		free(rets[i]);
	}
	llfree_validate(&upper);
	return success;
}

struct par_args {
	llfree_t *upper;
	uint64_t **frames;
	size_t core;
};

#undef CORES
#define CORES 4lu

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
		if (!llfree_is_ok(ret)) {
			assert(ret.error == LLFREE_ERR_MEMORY);
			break;
		}
		assert(ret.frame < total);

		(*(args->frames))[i] = ret.frame;
	}
	llfree_info("finish %zu (%zu)", args->core, i);
	return (void *)i;
}

declare_test(llfree_parallel_alloc_all)
{
	bool success = true;
	const uint64_t LENGTH = 8 << 18; // 8G

	lldrop llfree_t upper = llfree_new(CORES, LENGTH, LLFREE_INIT_FREE);

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

	check_equal("zu", llfree_frames(&upper), total);
	check_equal("zu", llfree_free_frames(&upper),
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
	return success;
}

declare_test(llfree_get_huge)
{
	bool success = true;
	llfree_result_t res;
	lldrop llfree_t upper =
		llfree_new(1, LLFREE_TREE_SIZE, LLFREE_INIT_FREE);

	llfree_info("get");
	res = llfree_get(&upper, 0, llflags(LLFREE_HUGE_ORDER));
	check(llfree_is_ok(res));

	llfree_info("get");
	res = llfree_get(&upper, 0, llflags(LLFREE_HUGE_ORDER));
	check(llfree_is_ok(res));

	llfree_validate(&upper);

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
		assert(llfree_is_ok(res));
		frames[i] = res.frame;
	}

	llfree_info("sync1 on %zu", data->core);
	atomic_fetch_add(&shared->sync1, 1);
	while (atom_load(&shared->sync1) < shared->cores) {
		spin_wait();
	}

	for (size_t i = 0; i < shared->frames_len; i++) {
		llfree_result_t res = llfree_put(shared->llfree, data->core,
						 frames[i], llflags(0));
		assert(llfree_is_ok(res));
	}

	return NULL;
}

declare_test(llfree_less_mem)
{
	bool success = true;
#undef CORES
#define CORES 4lu
#undef FRAMES
#define FRAMES 4096lu

	lldrop llfree_t upper = llfree_new(CORES, FRAMES, LLFREE_INIT_FREE);

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

	check_equal("zu", llfree_free_frames(&upper), FRAMES);
	llfree_validate(&upper);

	return success;
}
