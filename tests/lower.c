#include "lower.h"
#include "check.h"
#include "utils.h"

#include <stdlib.h>

#define bitfield_is_free(actual)                                             \
	check_equal_bitfield(actual, ((bitfield_t){ 0x0, 0x0, 0x0, 0x0, 0x0, \
						    0x0, 0x0, 0x0 }))

#define bitfield_is_free_n(actual, n)        \
	for (size_t i = 0; i < n; i++) {     \
		bitfield_is_free(actual[i]); \
	}

bool init_lower_test(uint8_t init)
{
	bool success = true;

	char *memory = NULL;
	size_t frames = 1024;
	lower_t actual;
	if (init != LLFREE_INIT_VOLATILE) {
		memory = llfree_ext_alloc(LLFREE_ALIGN,
					  frames * LLFREE_FRAME_SIZE);
		assert(memory != NULL);
	}
	lower_init(&actual, (uint64_t)memory / LLFREE_FRAME_SIZE, frames, init);
	lower_clear(&actual, true);
	check_equal(actual.childs_len, 2);

	bitfield_is_free(actual.fields[0]);
	check_equal(lower_free_frames(&actual), actual.frames);
	if (init == LLFREE_INIT_VOLATILE) {
		lower_drop(&actual);
		llfree_ext_free(LLFREE_ALIGN, frames, memory);
	}

	frames = 1023;
	if (init != LLFREE_INIT_VOLATILE) {
		memory = llfree_ext_alloc(LLFREE_ALIGN,
					  frames * LLFREE_FRAME_SIZE);
		assert(memory != NULL);
	}
	lower_init(&actual, (uint64_t)memory / LLFREE_FRAME_SIZE, frames, init);
	lower_clear(&actual, true);
	check_equal(actual.childs_len, 2);
	check_equal_bitfield(actual.fields[1],
			     ((bitfield_t){ 0, 0, 0, 0, 0, 0, 0,
					    init == LLFREE_INIT_VOLATILE ?
						    0x8000000000000000 :
						    0xC000000000000000 }));
	check_equal(lower_free_frames(&actual), actual.frames);
	if (init == LLFREE_INIT_VOLATILE) {
		lower_drop(&actual);
		llfree_ext_free(LLFREE_ALIGN, frames, memory);
	}

	frames = 632;
	if (init != LLFREE_INIT_VOLATILE) {
		memory = llfree_ext_alloc(LLFREE_ALIGN,
					  frames * LLFREE_FRAME_SIZE);
		assert(memory != NULL);
	}
	lower_init(&actual, (uint64_t)memory / LLFREE_FRAME_SIZE, frames, init);
	lower_clear(&actual, false);
	check_equal(actual.childs_len, 2);
	check_equal(actual.frames,
		    init == LLFREE_INIT_VOLATILE ? 632ul : 631ul);
	check_equal(lower_free_frames(&actual), 0);
	if (init == LLFREE_INIT_VOLATILE) {
		lower_drop(&actual);
		llfree_ext_free(LLFREE_ALIGN, frames, memory);
	}

	frames = 685161;
	if (init != LLFREE_INIT_VOLATILE) {
		memory = llfree_ext_alloc(LLFREE_ALIGN,
					  frames * LLFREE_FRAME_SIZE);
		assert(memory != NULL);
	}
	lower_init(&actual, (uint64_t)memory / LLFREE_FRAME_SIZE, frames, init);
	lower_clear(&actual, true);
	check_equal(actual.childs_len, 1339);
	bitfield_is_free_n(actual.fields, 1338);
	check_equal_bitfield(actual.fields[1338],
			     ((bitfield_t){ 0x0,
					    init == LLFREE_INIT_VOLATILE ?
						    0xfffffe0000000000 :
						    0xfffffffffff80000,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX,
					    UINT64_MAX }));
	check_equal(lower_free_frames(&actual),
		    init == LLFREE_INIT_VOLATILE ? 685161 : 685139);

	// check alignment

	check_equal_m((uint64_t)actual.children % LLFREE_CACHE_SIZE, 0ul,
		      "array must be aligned to cachesize");
	check_equal_m((uint64_t)actual.fields % LLFREE_CACHE_SIZE, 0ul,
		      "array must be aligned to cachesize");

	if (init == LLFREE_INIT_VOLATILE) {
		lower_drop(&actual);
		llfree_ext_free(LLFREE_ALIGN, frames, memory);
	}

	return success;
}

declare_test(lower_get)
{
	bool success = true;

	lower_t actual;
	lower_init(&actual, 0, 1360, LLFREE_INIT_VOLATILE);
	lower_clear(&actual, true);

	llfree_result_t ret;
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
	lower_init(&actual, 0, 2, LLFREE_INIT_VOLATILE);
	lower_clear(&actual, true);

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
	lower_init(&actual, 0, 166120, LLFREE_INIT_VOLATILE);
	lower_clear(&actual, true);

	ret = lower_get(&actual, 0, 0);
	check(ret.val == 0);
	ret = lower_get(&actual, 0, LLFREE_HUGE_ORDER);

	child_t child = atom_load(&actual.children[1]);
	check_equal(child.huge, true);
	check_equal(child.free, 0);
	check_equal_bitfield(actual.fields[1],
			     ((bitfield_t){ 0, 0, 0, 0, 0, 0, 0, 0 }));

	check_equal((int)ret.val, 1 << 9);

	return success;
}

declare_test(lower_put)
{
	bool success = true;

	lower_t actual;
	lower_init(&actual, 0, 1360, LLFREE_INIT_VOLATILE);
	lower_clear(&actual, true);

	uint64_t pfn;
	llfree_result_t ret;
	int order = 0;

	for (int i = 0; i < 957; i++) {
		ret = lower_get(&actual, 0, order);
		assert(llfree_result_ok(ret));
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
	check(llfree_result_ok(ret));
	check_equal_bitfield(actual.fields[0],
			     ((bitfield_t){ 0xfffffffffffffffe, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX,
					    UINT64_MAX }));
	check_equal_bitfield(actual.fields[1],
			     ((bitfield_t){ UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    0x1fffffffffffffff, 0x0 }));

	// wiederholtes put auf selbe stelle
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
	check(llfree_result_ok(ret));
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

	lower_t actual;
	lower_init(&actual, 0, 1360, LLFREE_INIT_VOLATILE);
	lower_clear(&actual, true);

	int ret;
	int order = 0;

	uint64_t pfn = 0;
	ret = lower_is_free(&actual, pfn, order);
	check_equal(ret, true);

	pfn = 910;
	ret = lower_is_free(&actual, pfn, order);
	check_equal(ret, true);

	lower_drop(&actual);

	lower_init(&actual, 0, 1360, LLFREE_INIT_VOLATILE);
	lower_clear(&actual, false);

	ret = lower_is_free(&actual, pfn, order);
	check_equal(ret, false);

	pfn = 910;
	ret = lower_is_free(&actual, pfn, order);
	check_equal(ret, false);
	assert(llfree_result_ok(lower_put(&actual, 513, order)));
	assert(llfree_result_ok(lower_put(&actual, 511, order)));
	ret = lower_is_free(&actual, 513, order);
	check_equal(ret, true);
	ret = lower_is_free(&actual, 511, order);
	check_equal(ret, true);
	ret = lower_is_free(&actual, 512, order);
	check_equal(ret, false);

	return success;
}

declare_test(lower_large)
{
	bool success = true;

	const size_t FRAMES = 128 * LLFREE_CHILD_SIZE;

	lower_t lower;
	lower_init(&lower, 0, FRAMES, LLFREE_INIT_VOLATILE);
	lower_clear(&lower, true);

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

		check_m(llfree_result_ok(pfn), "%ju -> %" PRId64, o, pfn.val);
		check_m(pfn.val % (1 << o) == 0, "%ju -> 0x%" PRIx64, o,
			pfn.val);
		frames[o] = pfn.val;
	}

	for (size_t o = 0; o <= LLFREE_MAX_ORDER; o++) {
		llfree_result_t ret = lower_put(&lower, frames[o], o);
		check_m(llfree_result_ok(ret), "%ju -> 0x%" PRIx64, o,
			frames[o]);
	}

	return success;
}

declare_test(lower_huge)
{
	bool success = true;

	lower_t actual;
	lower_init(&actual, 0, LLFREE_CHILD_SIZE * 60, LLFREE_INIT_VOLATILE);
	lower_clear(&actual, true);

	llfree_result_t pfn1 = lower_get(&actual, 0, LLFREE_HUGE_ORDER);
	check(llfree_result_ok(pfn1));
	uint64_t offset = pfn1.val % LLFREE_CHILD_SIZE;
	check_equal(offset, 0ul);
	llfree_result_t pfn2 = lower_get(&actual, 0, LLFREE_HUGE_ORDER);
	check(llfree_result_ok(pfn2));
	offset = pfn2.val % LLFREE_CHILD_SIZE;
	check_equal(offset, 0ul);
	check(pfn1.val != pfn2.val);
	check_equal(actual.frames - lower_free_frames(&actual),
		    2ul * LLFREE_CHILD_SIZE);

	// request a regular frame
	llfree_result_t regular = lower_get(&actual, 0, 0);
	assert(llfree_result_ok(regular));
	// regular frame cannot be returned as HP
	assert(!llfree_result_ok(
		lower_put(&actual, regular.val, LLFREE_HUGE_ORDER)));

	// this HF must be in another child than the regular frame.
	llfree_result_t pfn3 =
		lower_get(&actual, pfn_from_row(10), LLFREE_HUGE_ORDER);
	check(llfree_result_ok(pfn3));
	offset = pfn3.val % LLFREE_CHILD_SIZE;
	check_equal(offset, 0ul);
	check_equal((uint64_t)pfn3.val, 3 * LLFREE_CHILD_SIZE + actual.offset);

	// free regular page und try get this child as complete HP
	assert(llfree_result_ok(lower_put(&actual, regular.val, 0)));
	llfree_result_t pfn4 = lower_get(&actual, 0, LLFREE_HUGE_ORDER);
	check(llfree_result_ok(pfn4));
	check(pfn4.val == regular.val);

	llfree_result_t ret = lower_put(&actual, pfn2.val, LLFREE_HUGE_ORDER);
	check(llfree_result_ok(ret));

	// allocate the complete memory with HPs
	for (int i = 3; i < 60; ++i) {
		// get allocates only in chunks of 32 children. if there is no free HP in given chung it returns LLFREE_ERR_MEMORY
		llfree_result_t pfn =
			lower_get(&actual, i < 32 ? 0 : 32 * LLFREE_CHILD_SIZE,
				  LLFREE_HUGE_ORDER);
		check(llfree_result_ok(pfn));
	}

	check_equal_m(lower_free_frames(&actual), 0,
		      "fully allocated with huge frames");

	// reservation at full memory must fail
	llfree_result_t pfn = lower_get(&actual, 0, LLFREE_HUGE_ORDER);
	check(pfn.val == LLFREE_ERR_MEMORY);

	// return HP as regular Frame must succseed
	check(llfree_result_ok(lower_put(&actual, pfn2.val, 0)));
	// HP ist converted into regular frames so returning the whole page must fail
	check(lower_put(&actual, pfn2.val, LLFREE_HUGE_ORDER).val ==
	      LLFREE_ERR_ADDRESS);

	check(llfree_result_ok(
		lower_put(&actual, pfn1.val, LLFREE_HUGE_ORDER)));

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
	lower_t lower;
	lower_init(&lower, 0, FRAMES, LLFREE_INIT_VOLATILE);
	lower_clear(&lower, true);

	for (size_t i = 0; i < FRAMES / (1 << LLFREE_MAX_ORDER); ++i) {
		llfree_result_t pfn = lower_get(
			&lower, i * (1 << LLFREE_MAX_ORDER), LLFREE_MAX_ORDER);
		check_m(llfree_result_ok(pfn), "%ju", i);
	}

	check_equal(lower_free_frames(&lower), 0);

	for (size_t i = 0; i < FRAMES / (1 << LLFREE_MAX_ORDER); ++i) {
		llfree_result_t ret = lower_put(
			&lower, i * (1 << LLFREE_MAX_ORDER), LLFREE_MAX_ORDER);
		check_m(llfree_result_ok(ret), "%ju", i);
	}

	return success;
}

declare_test(lower_init_persistent)
{
	return init_lower_test(LLFREE_INIT_OVERWRITE);
}

declare_test(lower_init_volatile)
{
	return init_lower_test(LLFREE_INIT_VOLATILE);
}

declare_test(lower_free_all)
{
	bool success = true;
	const uint64_t len = (1 << 13) + 35; // 16 HP + 35 regular frames
	char *memory = llfree_ext_alloc(LLFREE_ALIGN, LLFREE_FRAME_SIZE * len);
	assert(memory != NULL);
	const uint64_t offset = (uint64_t)memory / LLFREE_FRAME_SIZE;

	lower_t lower;
	lower_init(&lower, offset, len, LLFREE_INIT_OVERWRITE);
	lower_clear(&lower, false);
	check_equal_m(lower.frames, len - 1,
		      "one frame for management structures");
	check_equal_m(lower_free_frames(&lower), 0ul,
		      "all_frames_are_allocated");

	// free all HPs
	llfree_result_t ret;
	for (int i = 0; i < 15; ++i) {
		ret = lower_put(&lower, i * 512, LLFREE_HUGE_ORDER);
		check(llfree_result_ok(ret));
	}
	check_equal_m(lower_free_frames(&lower), lower.frames - (512ul + 34),
		      "one allocated HF and the 34 regular frames");

	// free last HP as regular frame and regular frames
	const uint64_t start = 15 * 512;
	for (int i = 0; i < 512 + 34; ++i) {
		ret = lower_put(&lower, start + i, 0);
		check(llfree_result_ok(ret));
	}

	check_equal_m(lower_free_frames(&lower), lower.frames,
		      "lower should be completely free");

	llfree_ext_free(LLFREE_ALIGN, LLFREE_FRAME_SIZE * len, memory);
	return success;
}

declare_test(lower_persistent_init)
{
	bool success = true;
	size_t len = (1ul << 30) / LLFREE_FRAME_SIZE; // 1GiB
	char *mem = llfree_ext_alloc(LLFREE_ALIGN, len * LLFREE_FRAME_SIZE);
	assert(mem != NULL);
	llfree_info("mem: %p-%p (%lx)", mem, mem + len * LLFREE_FRAME_SIZE,
		    len);

	lower_t lower;
	lower_init(&lower, (uint64_t)mem / LLFREE_FRAME_SIZE, len,
		   LLFREE_INIT_OVERWRITE);
	lower_clear(&lower, true);

	llfree_info("childs %p, fields %p", lower.children, lower.fields);

	check_equal((uint64_t)lower.children % LLFREE_CACHE_SIZE, 0ul);
	check_equal((uint64_t)lower.fields % LLFREE_CACHE_SIZE, 0ul);

	check((uint64_t)lower.children > lower.offset * LLFREE_FRAME_SIZE);
	check((uint64_t)&lower.children[lower.childs_len] <
	      (lower.offset + len) * LLFREE_FRAME_SIZE);

	check((uint64_t)lower.fields > lower.offset * LLFREE_FRAME_SIZE);
	check((uint64_t)&lower.fields[lower.childs_len] <=
	      (lower.offset + len) * LLFREE_FRAME_SIZE);

	check_equal(lower_free_frames(&lower), lower.frames);

	llfree_result_t res = lower_get(&lower, 0, 0);
	check(llfree_result_ok(res));
	check_equal(lower_free_frames(&lower), lower.frames - 1);

	res = lower_get(&lower, 0, LLFREE_HUGE_ORDER);
	check(llfree_result_ok(res));
	check_equal(lower_free_frames(&lower),
		    lower.frames - 1 - (1 << LLFREE_HUGE_ORDER));

	llfree_info("Alloc");
	size_t n = LLFREE_TREE_CHILDREN - 1;
	for (size_t i = 0; i < n - 1; i++) {
		check(llfree_result_ok(lower_get(&lower, 0, 0)));
		check(llfree_result_ok(
			lower_get(&lower, 0, LLFREE_HUGE_ORDER)));
	}

	check_equal(n + n * 512, lower.frames - lower_free_frames(&lower));

	lower_drop(&lower);
	llfree_ext_free(LLFREE_ALIGN, len * LLFREE_FRAME_SIZE, mem);
	return success;
}
