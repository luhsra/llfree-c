#include "test.h"
#include "bitfield.h"

static inline bitfield_t bf(uint64_t b0, uint64_t b1, uint64_t b2, uint64_t b3,
			    uint64_t b4, uint64_t b5, uint64_t b6, uint64_t b7)
{
	return (bitfield_t){ { b0, b1, b2, b3, b4, b5, b6, b7 } };
}

bool first_zeros_aligned(uint64_t *v, size_t order, size_t *pos);

#define check_fza(v, order, expected_v, expected_pos)                          \
	({                                                                     \
		uint64_t val = (v);                                            \
		size_t pos;                                                    \
		bool found = first_zeros_aligned(&val, (order), &pos);         \
		check_equal_m("lu", val, (expected_v), "value");               \
		check_equal_m("d", found, (expected_pos) < SIZE_MAX, "found"); \
		if (found)                                                     \
			check_equal_m("zu", pos, (size_t)(expected_pos),       \
				      "position");                             \
	})

declare_test(bitfield_first_zeros)
{
	bool success = true;

	check_fza(0lu, 0lu, 0b1lu, 0ll);

	check_fza(0b0lu, 0lu, 0b1lu, 0ll);
	check_fza(0b0lu, 1lu, 0b11lu, 0ll);
	check_fza(0b0lu, 2lu, 0b1111lu, 0ll);
	check_fza(0b0lu, 3lu, 0xfflu, 0ll);
	check_fza(0b0lu, 4lu, 0xfffflu, 0ll);
	check_fza(0b0lu, 5lu, 0xfffffffflu, 0ll);
	check_fza(0b0lu, 6lu, 0xfffffffffffffffflu, 0ll);

	check_fza(0b1lu, 0lu, 0b11lu, 1ll);
	check_fza(0b1lu, 1lu, 0b1101lu, 2ll);
	check_fza(0b1lu, 2lu, 0xf1lu, 4ll);
	check_fza(0b1lu, 3lu, 0xff01lu, 8ll);
	check_fza(0b1lu, 4lu, 0xffff0001lu, 16ll);
	check_fza(0b1lu, 5lu, 0xffffffff00000001lu, 32ll);
	check_fza(0b1lu, 6lu, 0b1lu, SIZE_MAX);

	check_fza(0b101lu, 0lu, 0b111lu, 1ll);
	check_fza(0b10011lu, 1lu, 0b11111lu, 2ll);
	check_fza(0x10flu, 2lu, 0x1fflu, 4ll);
	check_fza(0x100fflu, 3lu, 0x1fffflu, 8ll);
	check_fza(0x10000fffflu, 4lu, 0x1fffffffflu, 16ll);
	check_fza(0x00000000ff00ff0flu, 5lu, 0xffffffffff00ff0flu, 32ll);
	check_fza(0b111100001100001110001111lu, 2lu,
		  0b111111111100001110001111lu, 16ll);

	// Upper bound
	check_fza(0x7ffffffffffffffflu, 0lu, 0xfffffffffffffffflu, 63ll);
	check_fza(0xfffffffffffffffflu, 0lu, 0xfffffffffffffffflu, SIZE_MAX);

	check_fza(0x3ffffffffffffffflu, 1lu, 0xfffffffffffffffflu, 62ll);
	check_fza(0x7ffffffffffffffflu, 1lu, 0x7ffffffffffffffflu, SIZE_MAX);

	check_fza(0x0ffffffffffffffflu, 2lu, 0xfffffffffffffffflu, 60ll);
	check_fza(0x1ffffffffffffffflu, 2lu, 0x1ffffffffffffffflu, SIZE_MAX);
	check_fza(0x3ffffffffffffffflu, 2lu, 0x3ffffffffffffffflu, SIZE_MAX);

	check_fza(0x00fffffffffffffflu, 3lu, 0xfffffffffffffffflu, 56ll);
	check_fza(0x0ffffffffffffffflu, 3lu, 0x0ffffffffffffffflu, SIZE_MAX);
	check_fza(0x1ffffffffffffffflu, 3lu, 0x1ffffffffffffffflu, SIZE_MAX);

	check_fza(0x0000fffffffffffflu, 4lu, 0xfffffffffffffffflu, 48ll);
	check_fza(0x0001fffffffffffflu, 4lu, 0x0001fffffffffffflu, SIZE_MAX);
	check_fza(0x00fffffffffffffflu, 4lu, 0x00fffffffffffffflu, SIZE_MAX);

	check_fza(0x00000000fffffffflu, 5lu, 0xfffffffffffffffflu, 32ll);
	check_fza(0x00000001fffffffflu, 5lu, 0x00000001fffffffflu, SIZE_MAX);
	check_fza(0x0000fffffffffffflu, 5lu, 0x0000fffffffffffflu, SIZE_MAX);

	check_fza(0lu, 6lu, 0xfffffffffffffffflu, 0ll);
	check_fza(1lu, 6lu, 1lu, SIZE_MAX);
	check_fza(0xa000000000000000lu, 6lu, 0xa000000000000000lu, SIZE_MAX);

	return success;
}

declare_test(bitfield_set_bit)
{
	bool success = true;

	bitfield_t actual = bf(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	bitfield_t expected = bf(0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);

	llfree_result_t ret = field_set_next(&actual, 0, 0);
	check_equal("lu", ret.frame, 0lu);
	check_equal_bitfield(actual, expected);

	actual = bf(0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	expected = bf(0x3, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);

	ret = field_set_next(&actual, 0, 0);
	check_equal("lu", ret.frame, 1lu);
	check_equal_bitfield(actual, expected);

	actual = bf(UINT64_MAX, 0xf, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	expected = bf(UINT64_MAX, 0x1f, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);

	ret = field_set_next(&actual, 0, 0);
	check_equal_m("lu", ret.frame, 68l, "call should be a success");
	check_equal_bitfield(actual, expected);

	actual = bf(UINT64_MAX, UINT64_MAX, UINT64_MAX, 0xfabdeadbeeffffff, 0x0,
		    0xdeadbeefdeadbeef, 0x0, 0x8000000000000000);
	expected = bf(UINT64_MAX, UINT64_MAX, UINT64_MAX, 0xfabdeadbefffffff,
		      0x0, 0xdeadbeefdeadbeef, 0x0, 0x8000000000000000);

	ret = field_set_next(&actual, 0, 0);
	check_equal_m("lu", ret.frame, 216lu, "call should be a success");
	check_equal_bitfield_m(actual, expected, "row 3 bit 24 -> e to f");

	actual = bf(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX,
		    UINT64_MAX, UINT64_MAX, UINT64_MAX);
	expected = bf(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX,
		      UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX);

	ret = field_set_next(&actual, 0, 0);
	check_equal_m("u", ret.error, LLFREE_ERR_MEMORY, "call should fail");
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
	check(!llfree_is_ok(ret));

	actual = bf(0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	expect = bf(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	pos = 0;

	ret = field_toggle(&actual, pos, 0, true);
	check_equal("lu", ret.frame, 0lu);
	check_equal_bitfield_m(actual, expect, "first one should be set to 0");

	actual = bf(0x1, 0xfacb8ffabf000000, 0xbadc007cd, 0x0, 0x0, 0x0, 0x0,
		    0xfffffacbfe975530);
	expect = bf(0x1, 0xfacb8ffabf000000, 0xbadc007cd, 0x0, 0x0, 0x0, 0x0,
		    0x7ffffacbfe975530);
	pos = 7ul * 64 + 63;

	ret = field_toggle(&actual, pos, 0, true);
	check_equal("lu", ret.frame, 0lu);
	check_equal_bitfield_m(actual, expect, "last bit should be set to 0");

	actual = bf(0x1, 0xfacb8ffabf000000, 0xbadc007cd, 0x0, 0x0, 0x0, 0x0,
		    0x7ffffacbfe975530);
	expect = bf(0x1, 0xfacb8ffabf000000, 0xaadc007cd, 0x0, 0x0, 0x0, 0x0,
		    0x7ffffacbfe975530);
	pos = 2ul * 64 + 32;

	ret = field_toggle(&actual, pos, 0, true);
	check_equal("lu", ret.frame, 0lu);
	check_equal_bitfield_m(actual, expect, "row 2 bit 31 -> b to a");

	actual = bf(0x1, 0xfacb8ffabf000000, 0xb2dc007cd, 0x0, 0x0, 0x0, 0x0,
		    0x7ffffacbfe975530);
	expect = bf(0x1, 0xfacb8ffabf000000, 0xb2dc007cd, 0x0, 0x0, 0x0, 0x0,
		    0x7ffffacbfe975530);
	pos = 4ul * 64 + 62;

	ret = field_toggle(&actual, pos, 0, true);
	check(!llfree_is_ok(ret));
	check_equal_bitfield_m(actual, expect, "no change");

	return success;
}

declare_test(bitfield_count_bits)
{
	bool success = true;

	bitfield_t actual = bf(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	bitfield_t expect = bf(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);

	size_t ret = field_count_ones(&actual);
	check_equal_m("zu", ret, 0lu, "no bits set");
	check_equal_bitfield_m(actual, expect, "no change!");

	actual = bf(0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8000000000000000);
	expect = bf(0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8000000000000000);

	ret = field_count_ones(&actual);
	check_equal_m("zu", ret, 2lu, "first and last bit set");
	check_equal_bitfield_m(actual, expect, "no change!");

	actual = bf(0x1, 0x0, 0x0, 0xfffdeadbeef4531, 0x0, 0x0, 0x0,
		    0x80000000f0000000);
	expect = bf(0x1, 0x0, 0x0, 0xfffdeadbeef4531, 0x0, 0x0, 0x0,
		    0x80000000f0000000);

	ret = field_count_ones(&actual);
	check_equal_m("zu", ret, 48lu, "some bits set");
	check_equal_bitfield_m(actual, expect, "no change!");

	actual = bf(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX,
		    UINT64_MAX, UINT64_MAX, UINT64_MAX);
	expect = bf(UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX,
		    UINT64_MAX, UINT64_MAX, UINT64_MAX);

	ret = field_count_ones(&actual);
	check_equal_m("zu", ret, 512lu, "all bits set");
	check_equal_bitfield_m(actual, expect, "no change!");

	return success;
}

declare_test(bitfield_free_bit)
{
	bool success = true;

	bitfield_t actual = bf(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0);
	bitfield_t expect = actual;

	bool ret = field_is_free(&actual, 0);
	check_equal("d", ret, true);
	check_equal_bitfield_m(actual, expect, "no change!");

	ret = field_is_free(&actual, 511);
	check_equal("d", ret, true);
	check_equal_bitfield_m(actual, expect, "no change!");

	actual = bf(0x618f66ac6dead122, UINT64_MAX, 0x0, 0x0, 0x0, 0x0, 0x0,
		    0x0);
	expect = actual;

	ret = field_is_free(&actual, 43);
	check_equal("d", ret, true);
	check_equal_bitfield_m(actual, expect, "no change!");

	ret = field_is_free(&actual, 42);
	check_equal("d", ret, false);
	check_equal_bitfield_m(actual, expect, "no change!");

	ret = field_is_free(&actual, 84);
	check_equal("d", ret, false);
	check_equal_bitfield_m(actual, expect, "no change!");

	return success;
}
