#include "bitfield_tests.h"

#include "check.h"
#include "bitfield.h"
#include "utils.h"

#define u64MAX 0xffffffffffffffff

bool set_Bit_test()
{
	bool success = true;

	bitfield_t actual =
		(bitfield_t){ { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 } };
	bitfield_t expected =
		(bitfield_t){ { 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 } };

	result_t ret = field_set_Bit(&actual, 0);
	check_equal((int)ret.val, 0);
	check_equal_bitfield_m(actual, expected, "first bit must be set");

	actual = (bitfield_t){ { 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 } };
	expected = (bitfield_t){ { 0x3, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 } };

	ret = field_set_Bit(&actual, 0);
	check_equal((int)ret.val, 1);
	check_equal_bitfield(actual, expected);

	actual = (bitfield_t){ { u64MAX, 0xf, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 } };
	expected =
		(bitfield_t){ { u64MAX, 0x1f, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 } };

	ret = field_set_Bit(&actual, 0);
	check_equal_m((int)ret.val, 68, "call should be a success");
	check_equal_bitfield(actual, expected);

	actual =
		(bitfield_t){ { u64MAX, u64MAX, u64MAX, 0xfabdeadbeeffffff, 0x0,
				0xdeadbeefdeadbeef, 0x0, 0x8000000000000000 } };
	expected =
		(bitfield_t){ { u64MAX, u64MAX, u64MAX, 0xfabdeadbefffffff, 0x0,
				0xdeadbeefdeadbeef, 0x0, 0x8000000000000000 } };

	ret = field_set_Bit(&actual, 0);
	check_equal_m((int)ret.val, 216, "call should be a success");
	check_equal_bitfield_m(actual, expected, "row 3 bit 24 -> e to f");

	actual = (bitfield_t){ { u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX,
				 u64MAX, u64MAX } };
	expected = (bitfield_t){ { u64MAX, u64MAX, u64MAX, u64MAX, u64MAX,
				   u64MAX, u64MAX, u64MAX } };

	ret = field_set_Bit(&actual, 0);
	check_equal_m((int)ret.val, ERR_MEMORY, "call should fail");
	check_equal_bitfield_m(actual, expected, "no change");

	return success;
}

bool reset_Bit_test()
{
	bool success = true;

	bitfield_t actual =
		(bitfield_t){ { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 } };
	bitfield_t expect = actual;
	uint64_t pos = 0;

	result_t ret = field_reset_bit(&actual, pos);
	check_equal_bitfield_m(actual, expect,
			       "no change if original Bit was already 0");
	check_equal((int)ret.val, ERR_ADDRESS);

	actual = (bitfield_t){ { 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 } };
	expect = (bitfield_t){ { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 } };
	pos = 0;

	ret = field_reset_bit(&actual, pos);
	check_equal((int)ret.val, 0);
	check_equal_bitfield_m(actual, expect, "first one should be set to 0");

	actual = (bitfield_t){ { 0x1, 0xfacb8ffabf000000, 0xbadc007cd, 0x0, 0x0,
				 0x0, 0x0, 0xfffffacbfe975530 } };
	expect = (bitfield_t){ { 0x1, 0xfacb8ffabf000000, 0xbadc007cd, 0x0, 0x0,
				 0x0, 0x0, 0x7ffffacbfe975530 } };
	pos = 7ul * 64 + 63;

	ret = field_reset_bit(&actual, pos);
	check_equal((int)ret.val, 0);
	check_equal_bitfield_m(actual, expect, "last bit should be set to 0");

	actual = (bitfield_t){ { 0x1, 0xfacb8ffabf000000, 0xbadc007cd, 0x0, 0x0,
				 0x0, 0x0, 0x7ffffacbfe975530 } };
	expect = (bitfield_t){ { 0x1, 0xfacb8ffabf000000, 0xaadc007cd, 0x0, 0x0,
				 0x0, 0x0, 0x7ffffacbfe975530 } };
	pos = 2ul * 64 + 32;

	ret = field_reset_bit(&actual, pos);
	check_equal((int)ret.val, 0);
	check_equal_bitfield_m(actual, expect, "row 2 bit 31 -> b to a");

	actual = (bitfield_t){ { 0x1, 0xfacb8ffabf000000, 0xb2dc007cd, 0x0, 0x0,
				 0x0, 0x0, 0x7ffffacbfe975530 } };
	expect = (bitfield_t){ { 0x1, 0xfacb8ffabf000000, 0xb2dc007cd, 0x0, 0x0,
				 0x0, 0x0, 0x7ffffacbfe975530 } };
	pos = 4ul * 64 + 62;

	ret = field_reset_bit(&actual, pos);
	check_equal((int)ret.val, ERR_ADDRESS);
	check_equal_bitfield_m(actual, expect, "no change");

	return success;
}

bool count_Set_Bits_test()
{
	bool success = true;

	bitfield_t actual =
		(bitfield_t){ { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 } };
	bitfield_t expect =
		(bitfield_t){ { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 } };

	int ret = field_count_bits(&actual);
	check_equal_m(ret, 0, "no bits set");
	check_equal_bitfield_m(actual, expect, "no change!");

	actual = (bitfield_t){ { 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
				 0x8000000000000000 } };
	expect = (bitfield_t){ { 0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
				 0x8000000000000000 } };

	ret = field_count_bits(&actual);
	check_equal_m(ret, 2, "first and last bit set");
	check_equal_bitfield_m(actual, expect, "no change!");

	actual = (bitfield_t){ { 0x1, 0x0, 0x0, 0xfffdeadbeef4531, 0x0, 0x0,
				 0x0, 0x80000000f0000000 } };
	expect = (bitfield_t){ { 0x1, 0x0, 0x0, 0xfffdeadbeef4531, 0x0, 0x0,
				 0x0, 0x80000000f0000000 } };

	ret = field_count_bits(&actual);
	check_equal_m(ret, 48, "some bits set");
	check_equal_bitfield_m(actual, expect, "no change!");

	actual = (bitfield_t){ { u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX,
				 u64MAX, u64MAX } };
	expect = (bitfield_t){ { u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX,
				 u64MAX, u64MAX } };

	ret = field_count_bits(&actual);
	check_equal_m(ret, 512, "all bits set");
	check_equal_bitfield_m(actual, expect, "no change!");

	return success;
}

bool is_free_bit_test()
{
	bool success = true;

	bitfield_t actual =
		(bitfield_t){ { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 } };
	bitfield_t expect = actual;

	int ret = field_is_free(&actual, 0);
	check_equal(ret, true);
	check_equal_bitfield_m(actual, expect, "no change!");

	ret = field_is_free(&actual, 511);
	check_equal(ret, true);
	check_equal_bitfield_m(actual, expect, "no change!");

	actual = (bitfield_t){ { 0x618f66ac6dead122, u64MAX, 0x0, 0x0, 0x0, 0x0,
				 0x0, 0x0 } };
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

int bitfield_tests(int *test_counter, int *fail_counter)
{
	run_test(set_Bit_test);
	run_test(reset_Bit_test);
	run_test(count_Set_Bits_test);
	run_test(is_free_bit_test);
	return 0;
}
