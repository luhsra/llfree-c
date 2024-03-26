#include "check.h"
#include "tree.h"
#include "utils.h"

#define equal_trees(actual, expect)            \
	check_equal(actual.free, expect.free); \
	check_equal(actual.reserved, expect.reserved)

declare_test(tree_atomic)
{
	bool success = true;
	_Atomic tree_t v;
	check(atomic_is_lock_free(&v));
	return success;
}

declare_test(tree_init)
{
	int success = true;

	int free = 498;
	bool reserved = false;

	tree_t actual = tree_new(free, reserved);
	check_equal(actual.free, free);
	check_equal(actual.reserved, reserved);

	free = LLFREE_TREE_SIZE; // maximum value
	reserved = false;
	actual = tree_new(free, reserved);
	check_equal(actual.free, free);
	check_equal(actual.reserved, reserved);

	free = 0; // minimum value
	reserved = true; // check if reserved is set
	actual = tree_new(free, reserved);
	check_equal(actual.free, free);
	check_equal(actual.reserved, reserved);

	return success;
}

declare_test(tree_reserve)
{
	int success = true;
	bool ret = false;

	tree_t actual = tree_new(7645, false);
	tree_t expect = tree_new(0, true);

	ret = tree_reserve(&actual, 0, (1 << LLFREE_TREE_ORDER));
	check(ret);
	equal_trees(actual, expect);

	// chek min counter value
	actual = tree_new(0, false);
	expect = tree_new(0, true);
	ret = tree_reserve(&actual, 0, (1 << LLFREE_TREE_ORDER));
	check(ret);
	equal_trees(actual, expect);

	// if already reserved
	actual = tree_new(456, true);
	expect = actual; // no change expected
	ret = tree_reserve(&actual, 0, (1 << LLFREE_TREE_ORDER));
	check_m(!ret, "already reserved");
	equal_trees(actual, expect);

	// max counter value
	actual = tree_new(LLFREE_TREE_SIZE, false);
	expect = tree_new(0, true);
	ret = tree_reserve(&actual, 0, (1 << LLFREE_TREE_ORDER));
	check(ret);
	equal_trees(actual, expect);

	return success;
}

declare_test(tree_unreserve)
{
	int success = true;

	int free;
	int frees;
	tree_t actual;
	tree_t expect;
	bool ret = false;

	free = 0;
	frees = 987;
	actual = tree_new(free, true);
	expect = tree_new(free + frees, false);
	ret = tree_writeback(&actual, frees);
	check(ret);
	equal_trees(actual, expect);

	free = 453;
	frees = 987;
	actual = tree_new(free, true);
	expect = tree_new(free + frees, false);
	ret = tree_writeback(&actual, frees);
	check(ret);
	equal_trees(actual, expect);

	return success;
}

declare_test(tree_inc)
{
	bool success = true;

	tree_t actual;
	tree_t expect;
	size_t order;
	int free;
	bool ret = false;

	order = 0;
	free = 0;
	actual = tree_new(free, false);
	expect = tree_new(free + (1 << order), false);
	ret = tree_inc(&actual, 1 << order);
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	free = LLFREE_TREE_SIZE - (1 << order); // max free for success
	actual = tree_new(free, false);
	expect = tree_new(free + (1 << order), false);
	ret = tree_inc(&actual, 1 << order);
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	free = 3456;
	// should be no difference if reserved is true
	actual = tree_new(free, true);
	expect = tree_new(free + (1 << order), true);
	ret = tree_inc(&actual, 1 << order);
	check(ret);
	equal_trees(actual, expect);

	order = LLFREE_HUGE_ORDER;
	free = 3456;
	actual = tree_new(free, true);
	expect = tree_new(free + (1 << order), true);
	ret = tree_inc(&actual, 1 << order);
	check(ret);
	equal_trees(actual, expect);

	order = LLFREE_HUGE_ORDER;
	free = LLFREE_TREE_SIZE - (1 << 9); // max free
	actual = tree_new(free, true);
	expect = tree_new(free + (1 << order), true);
	ret = tree_inc(&actual, 1 << order);
	check(ret);
	equal_trees(actual, expect);

	return success;
}
