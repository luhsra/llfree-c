#include "child_tests.h"
#include "check.h"

#include "../child.h"
#include "../bitfield.h"

#define check_counter(actual, expect)                \
	check_equal(actual.counter, expect.counter); \
	check_equal(actual.flag, expect.flag);

bool reserve_HP_test()
{
	bool success = true;
	check(sizeof(child_t) == 2, "right child size");

	child_t actual = child_init(512, false);
	child_t expect = child_init(0, true);

	int ret = child_reserve_HP(&actual);
	check_equal(ret, ERR_OK);
	check_equal(actual.counter, expect.counter);
	check_equal(actual.flag, expect.flag);

	actual = child_init(0, true);
	expect = child_init(0, true);

	ret = child_reserve_HP(&actual);
	check_equal_m(ret, ERR_MEMORY, "must fail if already set");
	check_equal(actual.counter, expect.counter);
	check_equal(actual.flag, expect.flag);

	actual = child_init(320, false);
	expect = child_init(320, false);

	ret = child_reserve_HP(&actual);
	check_equal_m(ret, ERR_MEMORY, "must fail if some frame are allocated");
	check_equal(actual.counter, expect.counter);
	check_equal(actual.flag, expect.flag);
	return success;
}

bool free_HP_test()
{
	bool success = true;

	child_t actual = child_init(0, true);
	child_t expect = child_init(FIELDSIZE, false);

	int ret = child_free_HP(&actual);
	check_equal(ret, ERR_OK);
	check_equal(actual.counter, expect.counter);
	check_equal(actual.flag, expect.flag);

	actual = child_init(0, false);
	expect = child_init(0, false);

	ret = child_free_HP(&actual);
	check_equal_m(ret, ERR_ADDRESS, "must fail if already reset");
	check_equal(actual.counter, expect.counter);
	check_equal(actual.flag, expect.flag);

	actual = child_init(320, true);
	expect = child_init(320, true);

	ret = child_free_HP(&actual);
	check_equal_m(
		ret, ERR_ADDRESS,
		"should not be possible to have a flag with a counter > 0");
	check_equal(actual.counter, expect.counter);
	check_equal(actual.flag, expect.flag);

	return success;
}

bool child_counter_inc_test()
{
	bool success = true;

	child_t actual = child_init(0x200, true);
	child_t expect = child_init(0x200, true);

	int ret = child_counter_inc(&actual);
	check_equal_m(ret, ERR_ADDRESS, "out of range");
	check_equal(actual.counter, expect.counter);
	check_equal(actual.flag, expect.flag);

	actual = child_init(5, false);
	expect = child_init(6, false);

	ret = child_counter_inc(&actual);
	check_equal(ret, ERR_OK);
	check_equal(actual.counter, expect.counter);
	check_equal(actual.flag, expect.flag);

	actual = child_init(0, true);
	expect = child_init(0, true);

	ret = child_counter_inc(&actual);
	check_equal(ret, ERR_ADDRESS);
	check_equal(actual.counter, expect.counter);
	check_equal(actual.flag, expect.flag);

	actual = child_init(0x01ff, false);
	expect = child_init(0x0200, false);

	ret = child_counter_inc(&actual);
	check_equal(ret, ERR_OK);
	check_equal(actual.counter, expect.counter);
	check_equal(actual.flag, expect.flag);

	return success;
}

bool child_counter_dec_test()
{
	bool success = true;

	child_t actual = child_init(0, false);
	child_t expect = child_init(0, false);

	int ret = child_counter_dec(&actual);
	check_equal_m(ret, ERR_MEMORY, "out of range");
	check_equal(actual.counter, expect.counter);
	check_equal(actual.flag, expect.flag);

	actual = child_init(9, false);
	expect = child_init(8, false);

	ret = child_counter_dec(&actual);
	check_equal(ret, ERR_OK);
	check_equal(actual.counter, expect.counter);
	check_equal(actual.flag, expect.flag);

	actual = child_init(FIELDSIZE, false);
	expect = child_init(FIELDSIZE - 1, false);

	ret = child_counter_dec(&actual);
	check_equal(ret, ERR_OK);
	check_equal(actual.counter, expect.counter);
	check_equal(actual.flag, expect.flag);

	actual = child_init(320, true);
	expect = child_init(320, true);

	ret = child_counter_dec(&actual);
	check_equal(ret, ERR_MEMORY);
	check_equal(actual.counter, expect.counter);
	check_equal(actual.flag, expect.flag);

	return success;
}

int child_tests(int *test_counter, int *fail_counter)
{
	run_test(reserve_HP_test);
	run_test(free_HP_test);
	run_test(child_counter_inc_test);
	run_test(child_counter_dec_test);

	return 0;
}
