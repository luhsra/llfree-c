#include "child_tests.h"
#include "check.h"

#include "child.h"
#include "bitfield.h"

#define check_counter(actual, expect)                \
	check_equal(actual.counter, expect.counter); \
	check_equal(actual.flag, expect.flag);

bool child_counter_inc_test()
{
	bool success = true;
	bool ret = false;

	child_t actual = child_new(0x200, true);
	ret = child_counter_inc(&actual, VOID);
	check_m(!ret, "out of range");

	actual = child_new(5, false);
	child_t expect = child_new(6, false);
	ret = child_counter_inc(&actual, VOID);
	check(ret);

	check_equal(actual.counter, expect.counter);
	check_equal(actual.huge, expect.huge);

	actual = child_new(0, true);
	ret = child_counter_inc(&actual, VOID);
	check_m(!ret, "is huge");

	actual = child_new(0x01ff, false);
	expect = child_new(0x0200, false);
	ret = child_counter_inc(&actual, VOID);
	check(ret);
	check_equal(actual.counter, expect.counter);
	check_equal(actual.huge, expect.huge);

	return success;
}

bool child_counter_dec_test()
{
	bool success = true;
	bool ret = false;

	child_t actual = child_new(0, false);
	ret = child_counter_dec(&actual, VOID);
	check_m(!ret, "out of range");

	actual = child_new(9, false);
	child_t expect = child_new(8, false);
	ret = child_counter_dec(&actual, VOID);
	check(ret);
	check_equal(actual.counter, expect.counter);
	check_equal(actual.huge, expect.huge);

	actual = child_new(FIELDSIZE, false);
	expect = child_new(FIELDSIZE - 1, false);
	ret = child_counter_dec(&actual, VOID);
	check(ret);
	check_equal(actual.counter, expect.counter);
	check_equal(actual.huge, expect.huge);

	actual = child_new(320, true);
	ret = child_counter_dec(&actual, VOID);
	check_m(!ret, "invalid state");

	return success;
}

int child_tests(int *test_counter, int *fail_counter)
{
	assert(({
		_Atomic child_t v;
		atomic_is_lock_free(&v);
	}));

	run_test(child_counter_inc_test);
	run_test(child_counter_dec_test);

	return 0;
}
