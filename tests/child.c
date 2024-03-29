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

	child_t actual = child_new(0x200, true, false);
	ret = child_inc(&actual, 0);
	check_m(!ret, "out of range");

	actual = child_new(5, false, false);
	child_t expect = child_new(6, false, false);
	ret = child_inc(&actual, 0);
	check(ret);

	check_equal(actual.free, expect.free);
	check_equal(actual.huge, expect.huge);

	actual = child_new(0, true, false);
	ret = child_inc(&actual, 0);
	check_m(!ret, "is huge");

	actual = child_new(0x01ff, false, false);
	expect = child_new(0x0200, false, false);
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

	child_t actual = child_new(0, false, false);
	ret = child_dec(&actual, 0);
	check_m(!ret, "out of range");

	actual = child_new(9, false, false);
	child_t expect = child_new(8, false, false);
	ret = child_dec(&actual, 0);
	check(ret);
	check_equal(actual.free, expect.free);
	check_equal(actual.huge, expect.huge);

	actual = child_new(LLFREE_CHILD_SIZE, false, false);
	expect = child_new(LLFREE_CHILD_SIZE - 1, false, false);
	ret = child_dec(&actual, 0);
	check(ret);
	check_equal(actual.free, expect.free);
	check_equal(actual.huge, expect.huge);

	actual = child_new(320, true, false);
	ret = child_dec(&actual, 0);
	check_m(!ret, "invalid state");

	return success;
}

declare_test(child_reserve_huge)
{
	bool success = true;
	bool ret = false;
	llflags_t flags;
	flags.get_unreported = true;

	child_t actual = child_new(LLFREE_CHILD_SIZE, true, false);
	ret = child_reserve_huge(&actual, flags);
	check_m(ret, "flags.get_unreported failed to get unreported");

	actual = child_new(LLFREE_CHILD_SIZE, true, true);
	ret = child_reserve_huge(&actual, flags);
	check_m(!ret, "flags.get_unreported got reported huge page");

	flags.get_unreported = false;
	actual = child_new(LLFREE_CHILD_SIZE, true, true);
	ret = child_reserve_huge(&actual, flags);
	check_m(!actual.reported,
		"child_reserve_huge does not reset reported flag");

	return success;
}
