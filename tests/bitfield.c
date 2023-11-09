#include "check.h"
#include "bitfield.h"
#include "utils.h"

static inline bitfield_t bf(uint64_t b0, uint64_t b1, uint64_t b2, uint64_t b3,
			    uint64_t b4, uint64_t b5, uint64_t b6, uint64_t b7)
{
	return (bitfield_t){ { b0, b1, b2, b3, b4, b5, b6, b7 } };
}

bool first_zeros_aligned(uint64_t *v, size_t order, size_t *pos);

#define check_fza(v, order, expected_v, expected_pos)                  \
	{                                                              \
		uint64_t val = (v);                                    \
		size_t pos;                                            \
		bool found = first_zeros_aligned(&val, (order), &pos); \
		check_equal_m(val, (expected_v), "value");             \
		check_equal_m(found, (expected_pos) >= 0, "found");    \
		if (found)                                             \
			check_equal_m(pos, (size_t)(expected_pos),     \
				      "position");                     \
	}

declare_test(bitfield_first_zeros)
{
	bool success = true;

	check_fza(0llu, 0lu, 0b1llu, 0ll);

	check_fza(0b0llu, 0lu, 0b1llu, 0ll);
	check_fza(0b0llu, 1lu, 0b11llu, 0ll);
	check_fza(0b0llu, 2lu, 0b1111llu, 0ll);
	check_fza(0b0llu, 3lu, 0xffllu, 0ll);
	check_fza(0b0llu, 4lu, 0xffffllu, 0ll);
	check_fza(0b0llu, 5lu, 0xffffffffllu, 0ll);
	check_fza(0b0llu, 6lu, 0xffffffffffffffffllu, 0ll);

	check_fza(0b1llu, 0lu, 0b11llu, 1ll);
	check_fza(0b1llu, 1lu, 0b1101llu, 2ll);
	check_fza(0b1llu, 2lu, 0xf1llu, 4ll);
	check_fza(0b1llu, 3lu, 0xff01llu, 8ll);
	check_fza(0b1llu, 4lu, 0xffff0001llu, 16ll);
	check_fza(0b1llu, 5lu, 0xffffffff00000001llu, 32ll);
	check_fza(0b1llu, 6lu, 0b1llu, LLFREE_ERR_MEMORY);

	check_fza(0b101llu, 0lu, 0b111llu, 1ll);
	check_fza(0b10011llu, 1lu, 0b11111llu, 2ll);
	check_fza(0x10fllu, 2lu, 0x1ffllu, 4ll);
	check_fza(0x100ffllu, 3lu, 0x1ffffllu, 8ll);
	check_fza(0x10000ffffllu, 4lu, 0x1ffffffffllu, 16ll);
	check_fza(0x00000000ff00ff0fllu, 5lu, 0xffffffffff00ff0fllu, 32ll);
	check_fza(0b111100001100001110001111llu, 2lu,
		  0b111111111100001110001111llu, 16ll);

	// Upper bound
	check_fza(0x7fffffffffffffffllu, 0lu, 0xffffffffffffffffllu, 63ll);
	check_fza(0xffffffffffffffffllu, 0lu, 0xffffffffffffffffllu,
		  LLFREE_ERR_MEMORY);

	check_fza(0x3fffffffffffffffllu, 1lu, 0xffffffffffffffffllu, 62ll);
	check_fza(0x7fffffffffffffffllu, 1lu, 0x7fffffffffffffffllu,
		  LLFREE_ERR_MEMORY);

	check_fza(0x0fffffffffffffffllu, 2lu, 0xffffffffffffffffllu, 60ll);
	check_fza(0x1fffffffffffffffllu, 2lu, 0x1fffffffffffffffllu,
		  LLFREE_ERR_MEMORY);
	check_fza(0x3fffffffffffffffllu, 2lu, 0x3fffffffffffffffllu,
		  LLFREE_ERR_MEMORY);

	check_fza(0x00ffffffffffffffllu, 3lu, 0xffffffffffffffffllu, 56ll);
	check_fza(0x0fffffffffffffffllu, 3lu, 0x0fffffffffffffffllu,
		  LLFREE_ERR_MEMORY);
	check_fza(0x1fffffffffffffffllu, 3lu, 0x1fffffffffffffffllu,
		  LLFREE_ERR_MEMORY);

	check_fza(0x0000ffffffffffffllu, 4lu, 0xffffffffffffffffllu, 48ll);
	check_fza(0x0001ffffffffffffllu, 4lu, 0x0001ffffffffffffllu,
		  LLFREE_ERR_MEMORY);
	check_fza(0x00ffffffffffffffllu, 4lu, 0x00ffffffffffffffllu,
		  LLFREE_ERR_MEMORY);

	check_fza(0x00000000ffffffffllu, 5lu, 0xffffffffffffffffllu, 32ll);
	check_fza(0x00000001ffffffffllu, 5lu, 0x00000001ffffffffllu,
		  LLFREE_ERR_MEMORY);
	check_fza(0x0000ffffffffffffllu, 5lu, 0x0000ffffffffffffllu,
		  LLFREE_ERR_MEMORY);

	check_fza(0llu, 6lu, 0xffffffffffffffffllu, 0ll);
	check_fza(1llu, 6lu, 1llu, LLFREE_ERR_MEMORY);
	check_fza(0xa000000000000000llu, 6lu, 0xa000000000000000llu,
		  LLFREE_ERR_MEMORY);

	return success;
}

declare_test(bitfield_set_bit)
{
	bool success = true;

	bitfield_t actual = bf(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	bitfield_t expected = bf(0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);

	llfree_result_t ret = field_set_next(&actual, 0, 0);
	check_equal(ret.val, 0);
	check_equal_bitfield(actual, expected);

	actual = bf(0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	expected = bf(0x3, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);

	ret = field_set_next(&actual, 0, 0);
	check_equal(ret.val, 1);
	check_equal_bitfield(actual, expected);

	actual = bf(UINT64_MAX, 0xf, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	expected = bf(UINT64_MAX, 0x1f, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);

	ret = field_set_next(&actual, 0, 0);
	check_equal_m(ret.val, 68l, "call should be a success");
	check_equal_bitfield(actual, expected);

	actual = bf(UINT64_MAX, UINT64_MAX, UINT64_MAX, 0xfabdeadbeeffffff, 0x0,
		    0xdeadbeefdeadbeef, 0x0, 0x8000000000000000);
	expected = bf(UINT64_MAX, UINT64_MAX, UINT64_MAX, 0xfabdeadbefffffff,
		      0x0, 0xdeadbeefdeadbeef, 0x0, 0x8000000000000000);

	ret = field_set_next(&actual, 0, 0);
	check_equal_m(ret.val, 216, "call should be a success");
	check_equal_bitfield_m(actual, expected, "row 3 bit 24 -> e to f");

	actual = bf(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX,
		    UINT64_MAX, UINT64_MAX, UINT64_MAX);
	expected = bf(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX,
		      UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX);

	ret = field_set_next(&actual, 0, 0);
	check_equal_m(ret.val, LLFREE_ERR_MEMORY, "call should fail");
	check_equal_bitfield_m(actual, expected, "no change");

	return success;
}

declare_test(bitfield_reset_bit)
{
	bool success = true;

	bitfield_t actual = bf(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	bitfield_t expect = actual;
	uint64_t pos = 0;

	llfree_result_t ret = field_toggle(&actual, pos, 0, true);
	check_equal_bitfield_m(actual, expect,
			       "no change if original Bit was already 0");
	check(!llfree_result_ok(ret));

	actual = bf(0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	expect = bf(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	pos = 0;

	ret = field_toggle(&actual, pos, 0, true);
	check_equal(ret.val, 0);
	check_equal_bitfield_m(actual, expect, "first one should be set to 0");

	actual = bf(0x1, 0xfacb8ffabf000000, 0xbadc007cd, 0x0, 0x0, 0x0, 0x0,
		    0xfffffacbfe975530);
	expect = bf(0x1, 0xfacb8ffabf000000, 0xbadc007cd, 0x0, 0x0, 0x0, 0x0,
		    0x7ffffacbfe975530);
	pos = 7ul * 64 + 63;

	ret = field_toggle(&actual, pos, 0, true);
	check_equal(ret.val, 0);
	check_equal_bitfield_m(actual, expect, "last bit should be set to 0");

	actual = bf(0x1, 0xfacb8ffabf000000, 0xbadc007cd, 0x0, 0x0, 0x0, 0x0,
		    0x7ffffacbfe975530);
	expect = bf(0x1, 0xfacb8ffabf000000, 0xaadc007cd, 0x0, 0x0, 0x0, 0x0,
		    0x7ffffacbfe975530);
	pos = 2ul * 64 + 32;

	ret = field_toggle(&actual, pos, 0, true);
	check_equal(ret.val, 0);
	check_equal_bitfield_m(actual, expect, "row 2 bit 31 -> b to a");

	actual = bf(0x1, 0xfacb8ffabf000000, 0xb2dc007cd, 0x0, 0x0, 0x0, 0x0,
		    0x7ffffacbfe975530);
	expect = bf(0x1, 0xfacb8ffabf000000, 0xb2dc007cd, 0x0, 0x0, 0x0, 0x0,
		    0x7ffffacbfe975530);
	pos = 4ul * 64 + 62;

	ret = field_toggle(&actual, pos, 0, true);
	check(!llfree_result_ok(ret));
	check_equal_bitfield_m(actual, expect, "no change");

	return success;
}

declare_test(bitfield_count_bits)
{
	bool success = true;

	bitfield_t actual = bf(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	bitfield_t expect = bf(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);

	int ret = field_count_ones(&actual);
	check_equal_m(ret, 0, "no bits set");
	check_equal_bitfield_m(actual, expect, "no change!");

	actual = bf(0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8000000000000000);
	expect = bf(0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8000000000000000);

	ret = field_count_ones(&actual);
	check_equal_m(ret, 2, "first and last bit set");
	check_equal_bitfield_m(actual, expect, "no change!");

	actual = bf(0x1, 0x0, 0x0, 0xfffdeadbeef4531, 0x0, 0x0, 0x0,
		    0x80000000f0000000);
	expect = bf(0x1, 0x0, 0x0, 0xfffdeadbeef4531, 0x0, 0x0, 0x0,
		    0x80000000f0000000);

	ret = field_count_ones(&actual);
	check_equal_m(ret, 48, "some bits set");
	check_equal_bitfield_m(actual, expect, "no change!");

	actual = bf(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX,
		    UINT64_MAX, UINT64_MAX, UINT64_MAX);
	expect = bf(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX,
		    UINT64_MAX, UINT64_MAX, UINT64_MAX);

	ret = field_count_ones(&actual);
	check_equal_m(ret, 512, "all bits set");
	check_equal_bitfield_m(actual, expect, "no change!");

	return success;
}

declare_test(bitfield_free_bit)
{
	bool success = true;

	bitfield_t actual = bf(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	bitfield_t expect = actual;

	bool ret = field_is_free(&actual, 0);
	check_equal(ret, true);
	check_equal_bitfield_m(actual, expect, "no change!");

	ret = field_is_free(&actual, 511);
	check_equal(ret, true);
	check_equal_bitfield_m(actual, expect, "no change!");

	actual = bf(0x618f66ac6dead122, UINT64_MAX, 0x0, 0x0, 0x0, 0x0, 0x0,
		    0x0);
	expect = actual;

	ret = field_is_free(&actual, 43);
	check_equal(ret, true);
	check_equal_bitfield_m(actual, expect, "no change!");

	ret = field_is_free(&actual, 42);
	check_equal(ret, false);
	check_equal_bitfield_m(actual, expect, "no change!");

	ret = field_is_free(&actual, 84);
	check_equal(ret, false);
	check_equal_bitfield_m(actual, expect, "no change!");

	return success;
}
