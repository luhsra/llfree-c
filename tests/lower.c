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
	if (init != INIT_VOLATILE) {
		memory = llc_ext_alloc(LLC_ALIGN, frames * FRAME_SIZE);
		assert(memory != NULL);
	}
	lower_init(&actual, (uint64_t)memory / FRAME_SIZE, frames, init);
	lower_clear(&actual, true);
	check_equal(actual.childs_len, 2);

	bitfield_is_free(actual.fields[0]);
	check_equal(lower_free_frames(&actual), actual.frames);
	if (init == INIT_VOLATILE) {
		lower_drop(&actual);
		llc_ext_free(LLC_ALIGN, frames, memory);
	}

	frames = 1023;
	if (init != INIT_VOLATILE) {
		memory = llc_ext_alloc(LLC_ALIGN, frames * FRAME_SIZE);
		assert(memory != NULL);
	}
	lower_init(&actual, (uint64_t)memory / FRAME_SIZE, frames, init);
	lower_clear(&actual, true);
	check_equal(actual.childs_len, 2);
	check_equal_bitfield(actual.fields[1],
			     ((bitfield_t){ 0, 0, 0, 0, 0, 0, 0,
					    init == INIT_VOLATILE ?
						    0x8000000000000000 :
						    0xC000000000000000 }));
	check_equal(lower_free_frames(&actual), actual.frames);
	if (init == INIT_VOLATILE) {
		lower_drop(&actual);
		llc_ext_free(LLC_ALIGN, frames, memory);
	}

	frames = 632;
	if (init != INIT_VOLATILE) {
		memory = llc_ext_alloc(LLC_ALIGN, frames * FRAME_SIZE);
		assert(memory != NULL);
	}
	lower_init(&actual, (uint64_t)memory / FRAME_SIZE, frames, init);
	lower_clear(&actual, false);
	check_equal(actual.childs_len, 2);
	check_equal(actual.frames, init == INIT_VOLATILE ? 632ul : 631ul);
	check_equal(lower_free_frames(&actual), 0);
	if (init == INIT_VOLATILE) {
		lower_drop(&actual);
		llc_ext_free(LLC_ALIGN, frames, memory);
	}

	frames = 685161;
	if (init != INIT_VOLATILE) {
		memory = llc_ext_alloc(LLC_ALIGN, frames * FRAME_SIZE);
		assert(memory != NULL);
	}
	lower_init(&actual, (uint64_t)memory / FRAME_SIZE, frames, init);
	lower_clear(&actual, true);
	check_equal(actual.childs_len, 1339);
	bitfield_is_free_n(actual.fields, 1338);
	check_equal_bitfield(
		actual.fields[1338],
		((bitfield_t){ 0x0,
			       init == INIT_VOLATILE ? 0xfffffe0000000000 :
						       0xfffffffffff80000,
			       UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX,
			       UINT64_MAX, UINT64_MAX }));
	check_equal(lower_free_frames(&actual),
		    init == INIT_VOLATILE ? 685161 : 685139);

	// check alignment

	check_equal_m((uint64_t)actual.children % CACHE_SIZE, 0ul,
		      "array must be aligned to cachesize");
	check_equal_m((uint64_t)actual.fields % CACHE_SIZE, 0ul,
		      "array must be aligned to cachesize");

	if (init == INIT_VOLATILE) {
		lower_drop(&actual);
		llc_ext_free(LLC_ALIGN, frames, memory);
	}

	return success;
}

declare_test(lower_get)
{
	bool success = true;

	lower_t actual;
	lower_init(&actual, 0, 1360, INIT_VOLATILE);
	lower_clear(&actual, true);

	result_t ret;
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
	lower_init(&actual, 0, 2, INIT_VOLATILE);
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
	check_equal((int)ret.val, ERR_MEMORY);
	check_equal_bitfield(actual.fields[0],
			     ((bitfield_t){ UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX, UINT64_MAX,
					    UINT64_MAX, UINT64_MAX }));

	lower_drop(&actual);
	lower_init(&actual, 0, 166120, INIT_VOLATILE);
	lower_clear(&actual, true);

	ret = lower_get(&actual, 0, 0);
	check(ret.val == 0);
	ret = lower_get(&actual, 0, HUGE_ORDER);

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
	lower_init(&actual, 0, 1360, INIT_VOLATILE);
	lower_clear(&actual, true);

	uint64_t pfn;
	result_t ret;
	int order = 0;

	for (int i = 0; i < 957; i++) {
		ret = lower_get(&actual, 0, order);
		assert(result_ok(ret));
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
	check(result_ok(ret));
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
	check_equal((int)ret.val, ERR_ADDRESS);
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
	check_equal((int)ret.val, ERR_ADDRESS);
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
	check(result_ok(ret));
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
	check_equal((int)ret.val, ERR_ADDRESS);
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
	lower_init(&actual, 0, 1360, INIT_VOLATILE);
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

	lower_init(&actual, 0, 1360, INIT_VOLATILE);
	lower_clear(&actual, false);

	ret = lower_is_free(&actual, pfn, order);
	check_equal(ret, false);

	pfn = 910;
	ret = lower_is_free(&actual, pfn, order);
	check_equal(ret, false);
	assert(result_ok(lower_put(&actual, 513, order)));
	assert(result_ok(lower_put(&actual, 511, order)));
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

	const size_t FRAMES = 128 * CHILD_SIZE;

	lower_t lower;
	lower_init(&lower, 0, FRAMES, INIT_VOLATILE);
	lower_clear(&lower, true);

	uint64_t frames[MAX_ORDER + 1];
	size_t tree = 0;
	for (size_t o = 0; o <= MAX_ORDER; o++) {
		result_t pfn;
		do {
			pfn = lower_get(&lower, tree << TREE_ORDER, o);
			if (pfn.val == ERR_MEMORY) {
				tree += 1;
				check(tree < FRAMES);
			}
		} while (pfn.val == ERR_MEMORY);

		check_m(result_ok(pfn), "%ju -> %" PRId64, o, pfn.val);
		check_m(pfn.val % (1 << o) == 0, "%ju -> 0x%" PRIx64, o,
			pfn.val);
		frames[o] = pfn.val;
	}

	for (size_t o = 0; o <= MAX_ORDER; o++) {
		result_t ret = lower_put(&lower, frames[o], o);
		check_m(result_ok(ret), "%ju -> 0x%" PRIx64, o, frames[o]);
	}

	return success;
}

declare_test(lower_huge)
{
	bool success = true;

	lower_t actual;
	lower_init(&actual, 0, CHILD_SIZE * 60, INIT_VOLATILE);
	lower_clear(&actual, true);

	result_t pfn1 = lower_get(&actual, 0, HUGE_ORDER);
	check(result_ok(pfn1));
	uint64_t offset = pfn1.val % CHILD_SIZE;
	check_equal(offset, 0ul);
	result_t pfn2 = lower_get(&actual, 0, HUGE_ORDER);
	check(result_ok(pfn2));
	offset = pfn2.val % CHILD_SIZE;
	check_equal(offset, 0ul);
	check(pfn1.val != pfn2.val);
	check_equal(actual.frames - lower_free_frames(&actual),
		    2ul * CHILD_SIZE);

	// request a regular frame
	result_t regular = lower_get(&actual, 0, 0);
	assert(result_ok(regular));
	// regular frame cannot be returned as HP
	assert(!result_ok(lower_put(&actual, regular.val, HUGE_ORDER)));

	// this HF must be in another child than the regular frame.
	result_t pfn3 = lower_get(&actual, pfn_from_row(10), HUGE_ORDER);
	check(result_ok(pfn3));
	offset = pfn3.val % CHILD_SIZE;
	check_equal(offset, 0ul);
	check_equal((uint64_t)pfn3.val, 3 * CHILD_SIZE + actual.offset);

	// free regular page und try get this child as complete HP
	assert(result_ok(lower_put(&actual, regular.val, 0)));
	result_t pfn4 = lower_get(&actual, 0, HUGE_ORDER);
	check(result_ok(pfn4));
	check(pfn4.val == regular.val);

	result_t ret = lower_put(&actual, pfn2.val, HUGE_ORDER);
	check(result_ok(ret));

	// allocate the complete memory with HPs
	for (int i = 3; i < 60; ++i) {
		// get allocates only in chunks of 32 children. if there is no free HP in given chung it returns ERR_MEMORY
		result_t pfn = lower_get(&actual, i < 32 ? 0 : 32 * CHILD_SIZE,
					 HUGE_ORDER);
		check(result_ok(pfn));
	}

	check_equal_m(lower_free_frames(&actual), 0,
		      "fully allocated with huge frames");

	// reservation at full memory must fail
	result_t pfn = lower_get(&actual, 0, HUGE_ORDER);
	check(pfn.val == ERR_MEMORY);

	// return HP as regular Frame must succseed
	check(result_ok(lower_put(&actual, pfn2.val, 0)));
	// HP ist converted into regular frames so returning the whole page must fail
	check(lower_put(&actual, pfn2.val, HUGE_ORDER).val == ERR_ADDRESS);

	check(result_ok(lower_put(&actual, pfn1.val, HUGE_ORDER)));

	// check if right amout of free regular frames are present
	check_equal(lower_free_frames(&actual), CHILD_SIZE + 1ul);

	// new acquired frame should be in same positon as the old no 1
	check(lower_get(&actual, 0, HUGE_ORDER).val == pfn1.val);

	return success;
}

declare_test(lower_max)
{
	bool success = true;

	const size_t FRAMES = CHILD_SIZE * 60;
	lower_t lower;
	lower_init(&lower, 0, FRAMES, INIT_VOLATILE);
	lower_clear(&lower, true);

	for (size_t i = 0; i < FRAMES / (1 << MAX_ORDER); ++i) {
		result_t pfn =
			lower_get(&lower, i * (1 << MAX_ORDER), MAX_ORDER);
		check_m(result_ok(pfn), "%ju", i);
	}

	check_equal(lower_free_frames(&lower), 0);

	for (size_t i = 0; i < FRAMES / (1 << MAX_ORDER); ++i) {
		result_t ret =
			lower_put(&lower, i * (1 << MAX_ORDER), MAX_ORDER);
		check_m(result_ok(ret), "%ju", i);
	}

	return success;
}

declare_test(lower_init_persistent)
{
	return init_lower_test(INIT_OVERWRITE);
}

declare_test(lower_init_volatile)
{
	return init_lower_test(INIT_VOLATILE);
}

declare_test(lower_free_all)
{
	bool success = true;
	const uint64_t len = (1 << 13) + 35; // 16 HP + 35 regular frames
	char *memory = llc_ext_alloc(LLC_ALIGN, FRAME_SIZE * len);
	assert(memory != NULL);
	const uint64_t offset = (uint64_t)memory / FRAME_SIZE;

	lower_t lower;
	lower_init(&lower, offset, len, INIT_OVERWRITE);
	lower_clear(&lower, false);
	check_equal_m(lower.frames, len - 1,
		      "one frame for management structures");
	check_equal_m(lower_free_frames(&lower), 0ul,
		      "all_frames_are_allocated");

	// free all HPs
	result_t ret;
	for (int i = 0; i < 15; ++i) {
		ret = lower_put(&lower, i * 512, HUGE_ORDER);
		check(result_ok(ret));
	}
	check_equal_m(lower_free_frames(&lower), lower.frames - (512ul + 34),
		      "one allocated HF and the 34 regular frames");

	// free last HP as regular frame and regular frames
	const uint64_t start = 15 * 512;
	for (int i = 0; i < 512 + 34; ++i) {
		ret = lower_put(&lower, start + i, 0);
		check(result_ok(ret));
	}

	check_equal_m(lower_free_frames(&lower), lower.frames,
		      "lower should be completely free");

	llc_ext_free(LLC_ALIGN, FRAME_SIZE * len, memory);
	return success;
}

declare_test(lower_persistent_init)
{
	bool success = true;
	size_t len = (1ul << 30) / FRAME_SIZE; // 1GiB
	char *mem = llc_ext_alloc(LLC_ALIGN, len * FRAME_SIZE);
	assert(mem != NULL);
	info("mem: %p-%p (%lx)", mem, mem + len * FRAME_SIZE, len);

	lower_t lower;
	lower_init(&lower, (uint64_t)mem / FRAME_SIZE, len, INIT_OVERWRITE);
	lower_clear(&lower, true);

	info("childs %p, fields %p", lower.children, lower.fields);

	check_equal((uint64_t)lower.children % CACHE_SIZE, 0ul);
	check_equal((uint64_t)lower.fields % CACHE_SIZE, 0ul);

	check((uint64_t)lower.children > lower.offset * FRAME_SIZE);
	check((uint64_t)&lower.children[lower.childs_len] <
	      (lower.offset + len) * FRAME_SIZE);

	check((uint64_t)lower.fields > lower.offset * FRAME_SIZE);
	check((uint64_t)&lower.fields[lower.childs_len] <=
	      (lower.offset + len) * FRAME_SIZE);

	check_equal(lower_free_frames(&lower), lower.frames);

	result_t res = lower_get(&lower, 0, 0);
	check(result_ok(res));
	check_equal(lower_free_frames(&lower), lower.frames - 1);

	res = lower_get(&lower, 0, HUGE_ORDER);
	check(result_ok(res));
	check_equal(lower_free_frames(&lower),
		    lower.frames - 1 - (1 << HUGE_ORDER));

	info("Alloc");
	size_t n = TREE_CHILDREN - 1;
	for (size_t i = 0; i < n - 1; i++) {
		check(result_ok(lower_get(&lower, 0, 0)));
		check(result_ok(lower_get(&lower, 0, HUGE_ORDER)));
	}

	check_equal(n + n * 512, lower.frames - lower_free_frames(&lower));

	lower_drop(&lower);
	llc_ext_free(LLC_ALIGN, len * FRAME_SIZE, mem);
	return success;
}
