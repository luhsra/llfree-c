#include "child.h"
#include "check.h"
#include "lower.h"

#define check_counter(actual, expect)                \
	check_equal(actual.counter, expect.counter); \
	check_equal(actual.flag, expect.flag);

declare_test(child_atomic)
{
	bool success = true;
	check(({
		_Atomic child_t v;
		atomic_is_lock_free(&v);
	}));
	check(sizeof(children_t) % LLFREE_CACHE_SIZE == 0);
	return success;
}

declare_test(child_counter_inc)
{
	bool success = true;
	bool ret = false;

	child_t actual = child_new(0x200, true);
	ret = child_inc(&actual, 0);
	check_m(!ret, "out of range");

	actual = child_new(5, false);
	child_t expect = child_new(6, false);
	ret = child_inc(&actual, 0);
	check(ret);

	check_equal(actual.free, expect.free);
	check_equal(actual.huge, expect.huge);

	actual = child_new(0, true);
	ret = child_inc(&actual, 0);
	check_m(!ret, "is huge");

	actual = child_new(0x01ff, false);
	expect = child_new(0x0200, false);
	ret = child_inc(&actual, 0);
	check(ret);
	check_equal(actual.free, expect.free);
	check_equal(actual.huge, expect.huge);

	return success;
}

declare_test(child_counter_dec)
{
	bool success = true;
	bool ret = false;

	child_t actual = child_new(0, false);
	ret = child_dec(&actual, 0);
	check_m(!ret, "out of range");

	actual = child_new(9, false);
	child_t expect = child_new(8, false);
	ret = child_dec(&actual, 0);
	check(ret);
	check_equal(actual.free, expect.free);
	check_equal(actual.huge, expect.huge);

	actual = child_new(LLFREE_CHILD_SIZE, false);
	expect = child_new(LLFREE_CHILD_SIZE - 1, false);
	ret = child_dec(&actual, 0);
	check(ret);
	check_equal(actual.free, expect.free);
	check_equal(actual.huge, expect.huge);

	actual = child_new(320, true);
	ret = child_dec(&actual, 0);
	check_m(!ret, "invalid state");

	return success;
}
