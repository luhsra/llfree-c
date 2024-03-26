#include "lower.h"
#include "check.h"
#include "utils.h"

#include <assert.h>
#include <stdlib.h>

#define bitfield_is_free(actual)                                             \
	check_equal_bitfield(actual, ((bitfield_t){ 0x0, 0x0, 0x0, 0x0, 0x0, \
						    0x0, 0x0, 0x0 }))

#define bitfield_is_free_n(actual, n)        \
	for (size_t i = 0; i < n; i++) {     \
		bitfield_is_free(actual[i]); \
	}

static inline lower_t lower_new(size_t frames, uint8_t init)
{
	lower_t actual;
	uint8_t *primary = llfree_ext_alloc(LLFREE_CACHE_SIZE,
					    lower_metadata_size(frames));
	llfree_result_t ret = lower_init(&actual, frames, init, primary);
	assert(llfree_ok(ret));
	return actual;
}

static inline void lower_drop(lower_t *self)
{
	assert(self != NULL);
	llfree_ext_free(LLFREE_CACHE_SIZE, lower_metadata_size(self->frames),
			lower_metadata(self));
}

declare_test(lower_init)
{
	bool success = true;

	size_t frames = 1024;
	lower_t actual = lower_new(frames, LLFREE_INIT_FREE);

	check_equal(actual.children_len, 2);
	bitfield_is_free(actual.fields[0]);
	check_equal(lower_free_frames(&actual), actual.frames);
	lower_drop(&actual);

	frames = 1023;
	actual = lower_new(frames, LLFREE_INIT_FREE);

	check_equal(actual.children_len, 2);
	check_equal_bitfield(
		actual.fields[1],
		((bitfield_t){ 0, 0, 0, 0, 0, 0, 0, 0x8000000000000000 }));
	check_equal(lower_free_frames(&actual), actual.frames);
	lower_drop(&actual);

	frames = 632;
	actual = lower_new(frames, LLFREE_INIT_ALLOC);

	check_equal(actual.children_len, 2);
	check_equal(actual.frames, 632ul);
	check_equal(lower_free_frames(&actual), 0);
	lower_drop(&actual);

	frames = 685161;
	actual = lower_new(frames, LLFREE_INIT_FREE);

	check_equal(actual.children_len, 1339);
	bitfield_is_free_n(actual.fields, 1338);
	check_equal_bitfield(actual.fields[1338],
			     ((bitfield_t){ 0x0, 0xfffffe0000000000, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX }));
	check_equal(lower_free_frames(&actual), 685161);

	// check alignment

	check_equal_m((uint64_t)actual.children % LLFREE_CACHE_SIZE, 0ul,
		      "array must be aligned to cachesize");
	check_equal_m((uint64_t)actual.fields % LLFREE_CACHE_SIZE, 0ul,
		      "array must be aligned to cachesize");
	lower_drop(&actual);

	return success;
}

declare_test(lower_get)
{
	bool success = true;
	llfree_result_t ret;

	size_t frames = 1360;
	lower_t actual = lower_new(frames, LLFREE_INIT_FREE);

	int order = 0;

	ret = lower_get(&actual, 0, order);
	check_equal((int)ret.val, 0);
	check_equal_bitfield(
		actual.fields[0],
		((bitfield_t){ 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }));
	return success;

	ret = lower_get(&actual, 0, order);
	check_equal((int)ret.val, 1);
	check_equal_bitfield(
		actual.fields[0],
		((bitfield_t){ 0x3, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }));

	ret = lower_get(&actual, 320, order);
	check_equal((int)ret.val, 320);
	check_equal_bitfield(
		actual.fields[0],
		((bitfield_t){ 0x3, 0x0, 0x0, 0x0, 0x0, 0x1, 0x0, 0x0 }));

	for (int i = 0; i < 954; i++) {
		ret = lower_get(&actual, 0, order);
		check_equal((int)ret.val, (i + (i < 318 ? 2 : 3)));
	}
	check_equal_bitfield(actual.fields[0],
			     ((bitfield_t){ UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX }));
	check_equal_bitfield(actual.fields[1],
			     ((bitfield_t){ UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    0x1fffffffffffffff, 0x0 }));

	lower_drop(&actual);

	frames = 2;
	actual = lower_new(frames, LLFREE_INIT_FREE);

	ret = lower_get(&actual, 0, order);
	check_equal((int)ret.val, 0);
	check_equal_bitfield(actual.fields[0],
			     ((bitfield_t){ 0xfffffffffffffffd, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX,
					    UINT64_MAX }));

	ret = lower_get(&actual, 0, order);
	check_equal((int)ret.val, 1);
	check_equal_bitfield(actual.fields[0],
			     ((bitfield_t){ UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX }));

	ret = lower_get(&actual, 0, order);
	check_equal((int)ret.val, LLFREE_ERR_MEMORY);
	check_equal_bitfield(actual.fields[0],
			     ((bitfield_t){ UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX }));
	lower_drop(&actual);

	frames = 166120;
	actual = lower_new(frames, LLFREE_INIT_FREE);

	ret = lower_get(&actual, 0, 0);
	check(ret.val == 0);
	ret = lower_get(&actual, 0, LLFREE_HUGE_ORDER);

	child_t child = atom_load(&actual.children[1]);
	check_equal(child.huge, true);
	check_equal(child.free, 0);
	check_equal_bitfield(actual.fields[1],
			     ((bitfield_t){ 0, 0, 0, 0, 0, 0, 0, 0 }));

	check_equal((int)ret.val, 1 << 9);
	lower_drop(&actual);

	return success;
}

declare_test(lower_put)
{
	bool success = true;

	lower_t actual = lower_new(1360, LLFREE_INIT_FREE);

	uint64_t pfn;
	llfree_result_t ret;
	int order = 0;

	for (int i = 0; i < 957; i++) {
		ret = lower_get(&actual, 0, order);
		assert(llfree_ok(ret));
	}
	check_equal_bitfield(actual.fields[0],
			     ((bitfield_t){ UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX }));
	check_equal_bitfield(actual.fields[1],
			     ((bitfield_t){ UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    0x1fffffffffffffff, 0x0 }));

	pfn = 0;
	ret = lower_put(&actual, pfn, order);
	check(llfree_ok(ret));
	check_equal_bitfield(actual.fields[0],
			     ((bitfield_t){ 0xfffffffffffffffe, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX,
					    UINT64_MAX }));
	check_equal_bitfield(actual.fields[1],
			     ((bitfield_t){ UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    0x1fffffffffffffff, 0x0 }));

	// repeat the same address
	pfn = 0;
	ret = lower_put(&actual, pfn, order);
	check_equal((int)ret.val, LLFREE_ERR_ADDRESS);
	check_equal_bitfield(actual.fields[0],
			     ((bitfield_t){ 0xfffffffffffffffe, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX,
					    UINT64_MAX }));
	check_equal_bitfield(actual.fields[1],
			     ((bitfield_t){ UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    0x1fffffffffffffff, 0x0 }));

	pfn = 957;
	ret = lower_put(&actual, pfn, order);
	check_equal((int)ret.val, LLFREE_ERR_ADDRESS);
	check_equal_bitfield(actual.fields[0],
			     ((bitfield_t){ 0xfffffffffffffffe, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX,
					    UINT64_MAX }));
	check_equal_bitfield(actual.fields[1],
			     ((bitfield_t){ UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    0x1fffffffffffffff, 0x0 }));

	pfn = 561;
	ret = lower_put(&actual, pfn, order);
	check(llfree_ok(ret));
	check_equal_bitfield(actual.fields[0],
			     ((bitfield_t){ 0xfffffffffffffffe, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX,
					    UINT64_MAX }));
	check_equal_bitfield(actual.fields[1],
			     ((bitfield_t){ 0xfffdffffffffffff, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, 0x1fffffffffffffff,
					    0x0 }));

	// outside the range
	pfn = 1361;
	ret = lower_put(&actual, pfn, order);
	check_equal((int)ret.val, LLFREE_ERR_ADDRESS);
	check_equal_bitfield(actual.fields[0],
			     ((bitfield_t){ 0xfffffffffffffffe, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX,
					    UINT64_MAX }));
	check_equal_bitfield(actual.fields[1],
			     ((bitfield_t){ 0xfffdffffffffffff, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, 0x1fffffffffffffff,
					    0x0 }));

	lower_drop(&actual);
	return success;
}

declare_test(lower_is_free)
{
	bool success = true;

	lower_t actual = lower_new(1360, LLFREE_INIT_FREE);

	assert(lower_is_free(&actual, 0, 0));
	assert(lower_is_free(&actual, 910, 0));

	lower_drop(&actual);

	actual = lower_new(1360, LLFREE_INIT_ALLOC);

	assert(!lower_is_free(&actual, 0, 0));
	assert(!lower_is_free(&actual, 513, 0));
	assert(!lower_is_free(&actual, 511, 0));

	assert(llfree_ok(lower_put(&actual, 513, 0)));
	assert(llfree_ok(lower_put(&actual, 511, 0)));

	assert(lower_is_free(&actual, 513, 0));
	assert(lower_is_free(&actual, 511, 0));
	assert(!lower_is_free(&actual, 512, 0));

	lower_drop(&actual);
	return success;
}

declare_test(lower_large)
{
	bool success = true;

	const size_t FRAMES = 128lu * LLFREE_CHILD_SIZE;

	lower_t lower = lower_new(FRAMES, LLFREE_INIT_FREE);

	uint64_t frames[LLFREE_MAX_ORDER + 1];
	size_t tree = 0;
	for (size_t o = 0; o <= LLFREE_MAX_ORDER; o++) {
		llfree_result_t pfn;
		do {
			pfn = lower_get(&lower, tree << LLFREE_TREE_ORDER, o);
			if (pfn.val == LLFREE_ERR_MEMORY) {
				tree += 1;
				check(tree < FRAMES);
			}
		} while (pfn.val == LLFREE_ERR_MEMORY);

		check_m(llfree_ok(pfn), "%zu -> %" PRId64, o, pfn.val);
		check_m(pfn.val % (1 << o) == 0, "%zu -> 0x%" PRIx64, o,
			pfn.val);
		frames[o] = pfn.val;
	}

	for (size_t o = 0; o <= LLFREE_MAX_ORDER; o++) {
		llfree_result_t ret = lower_put(&lower, frames[o], o);
		check_m(llfree_ok(ret), "%zu -> 0x%" PRIx64, o, frames[o]);
	}

	return success;
}

declare_test(lower_huge)
{
	bool success = true;

	const size_t FRAMES = LLFREE_CHILD_SIZE * 60;
	lower_t actual = lower_new(FRAMES, LLFREE_INIT_FREE);

	llfree_result_t pfn1 = lower_get(&actual, 0, LLFREE_HUGE_ORDER);
	check(llfree_ok(pfn1));
	uint64_t offset = pfn1.val % LLFREE_CHILD_SIZE;
	check_equal(offset, 0ul);
	llfree_result_t pfn2 = lower_get(&actual, 0, LLFREE_HUGE_ORDER);
	check(llfree_ok(pfn2));
	offset = pfn2.val % LLFREE_CHILD_SIZE;
	check_equal(offset, 0ul);
	check(pfn1.val != pfn2.val);
	check_equal(actual.frames - lower_free_frames(&actual),
		    2ul * LLFREE_CHILD_SIZE);

	// request a regular frame
	llfree_result_t regular = lower_get(&actual, 0, 0);
	assert(llfree_ok(regular));
	// regular frame cannot be returned as HP
	assert(!llfree_ok(lower_put(&actual, regular.val, LLFREE_HUGE_ORDER)));

	// this HF must be in another child than the regular frame.
	llfree_result_t pfn3 =
		lower_get(&actual, pfn_from_row(10), LLFREE_HUGE_ORDER);
	check(llfree_ok(pfn3));
	offset = pfn3.val % LLFREE_CHILD_SIZE;
	check_equal(offset, 0ul);
	check_equal((uint64_t)pfn3.val, 3 * LLFREE_CHILD_SIZE);

	// free regular page und try get this child as complete HP
	assert(llfree_ok(lower_put(&actual, regular.val, 0)));
	llfree_result_t pfn4 = lower_get(&actual, 0, LLFREE_HUGE_ORDER);
	check(llfree_ok(pfn4));
	check(pfn4.val == regular.val);

	llfree_result_t ret = lower_put(&actual, pfn2.val, LLFREE_HUGE_ORDER);
	check(llfree_ok(ret));

	// allocate the complete memory with HPs
	for (int i = 3; i < 60; ++i) {
		// get allocates only in chunks of N children. if there is no free HP in given chung it returns LLFREE_ERR_MEMORY
		llfree_result_t pfn = lower_get(
			&actual, (i / LLFREE_TREE_CHILDREN) * LLFREE_TREE_SIZE, LLFREE_HUGE_ORDER);
		check(llfree_ok(pfn));
	}

	check_equal_m(lower_free_frames(&actual), 0,
		      "fully allocated with huge frames");

	// reservation at full memory must fail
	llfree_result_t pfn = lower_get(&actual, 0, LLFREE_HUGE_ORDER);
	check(pfn.val == LLFREE_ERR_MEMORY);

	// return HP as regular Frame must succseed
	check(llfree_ok(lower_put(&actual, pfn2.val, 0)));
	// HP ist converted into regular frames so returning the whole page must fail
	check(lower_put(&actual, pfn2.val, LLFREE_HUGE_ORDER).val ==
	      LLFREE_ERR_ADDRESS);

	check(llfree_ok(lower_put(&actual, pfn1.val, LLFREE_HUGE_ORDER)));

	// check if right amout of free regular frames are present
	check_equal(lower_free_frames(&actual), LLFREE_CHILD_SIZE + 1ul);

	// new acquired frame should be in same positon as the old no 1
	check(lower_get(&actual, 0, LLFREE_HUGE_ORDER).val == pfn1.val);

	return success;
}

declare_test(lower_max)
{
	bool success = true;

	const size_t FRAMES = LLFREE_CHILD_SIZE * 60;
	lower_t lower = lower_new(FRAMES, LLFREE_INIT_FREE);

	for (size_t i = 0; i < FRAMES / (1 << LLFREE_MAX_ORDER); ++i) {
		llfree_result_t pfn = lower_get(
			&lower, i * (1 << LLFREE_MAX_ORDER), LLFREE_MAX_ORDER);
		check_m(llfree_ok(pfn), "%zu", i);
	}

	check_equal(lower_free_frames(&lower), 0);

	for (size_t i = 0; i < FRAMES / (1 << LLFREE_MAX_ORDER); ++i) {
		llfree_result_t ret = lower_put(
			&lower, i * (1 << LLFREE_MAX_ORDER), LLFREE_MAX_ORDER);
		check_m(llfree_ok(ret), "%zu", i);
	}

	return success;
}

declare_test(lower_free_all)
{
	bool success = true;
	const uint64_t len = (1 << 13) + 35; // 16 HP + 35 regular frames
	lower_t lower = lower_new(len, LLFREE_INIT_ALLOC);
	check_equal(lower.frames, len);
	check_equal_m(lower_free_frames(&lower), 0ul,
		      "all_frames_are_allocated");

	// free all HPs
	llfree_result_t ret;
	for (size_t i = 0; i < 15; ++i) {
		ret = lower_put(&lower, i * 512, LLFREE_HUGE_ORDER);
		check(llfree_ok(ret));
	}
	check_equal_m(lower_free_frames(&lower), lower.frames - (512ul + 35),
		      "one allocated HF and the 35 regular frames");

	// free last HP as regular frame and regular frames
	const uint64_t start = 15 * 512;
	for (size_t i = 0; i < 512 + 35; ++i) {
		ret = lower_put(&lower, start + i, 0);
		check(llfree_ok(ret));
	}

	check_equal_m(lower_free_frames(&lower), lower.frames,
		      "lower should be completely free");

	return success;
}
