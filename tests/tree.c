#include "check.h"
#include "tree.h"

#define equal_trees(actual, expect)            \
	check_equal("u", actual.free, expect.free); \
	check_equal("u", actual.reserved, expect.reserved)

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

	treeF_t free = 498;
	bool reserved = false;

	tree_t actual = tree_new(free, reserved, TREE_FIXED, 0);
	check_equal("u", actual.free, free);
	check_equal("u", actual.reserved, reserved);

	free = LLFREE_TREE_SIZE; // maximum value
	reserved = false;
	actual = tree_new(free, reserved, TREE_FIXED, 0);
	check_equal("u", actual.free, free);
	check_equal("u", actual.reserved, reserved);

	free = 0; // minimum value
	reserved = true; // check if reserved is set
	actual = tree_new(free, reserved, TREE_FIXED, 0);
	check_equal("u", actual.free, free);
	check_equal("u", actual.reserved, reserved);

	return success;
}

declare_test(tree_reserve)
{
	int success = true;
	bool ret = false;

	tree_t actual = tree_new(764, false, TREE_FIXED, 0);
	tree_t expect = tree_new(0, true, TREE_FIXED, 0);

	ret = tree_reserve(&actual, 0, (1 << LLFREE_TREE_ORDER), TREE_FIXED);
	check(ret);
	equal_trees(actual, expect);

	// chek min counter value
	actual = tree_new(0, false, TREE_FIXED, 0);
	expect = tree_new(0, true, TREE_FIXED, 0);
	ret = tree_reserve(&actual, 0, (1 << LLFREE_TREE_ORDER), TREE_FIXED);
	check(ret);
	equal_trees(actual, expect);

	// if already reserved
	actual = tree_new(456, true, TREE_FIXED, 0);
	expect = actual; // no change expected
	ret = tree_reserve(&actual, 0, (1 << LLFREE_TREE_ORDER), TREE_FIXED);
	check_m(!ret, "already reserved");
	equal_trees(actual, expect);

	// max counter value
	actual = tree_new(LLFREE_TREE_SIZE, false, TREE_FIXED, 0);
	expect = tree_new(0, true, TREE_FIXED, 0);
	ret = tree_reserve(&actual, 0, (1 << LLFREE_TREE_ORDER), TREE_FIXED);
	check(ret);
	equal_trees(actual, expect);

	return success;
}

declare_test(tree_unreserve)
{
	int success = true;

	treeF_t free;
	treeF_t frees;
	tree_t actual;
	tree_t expect;
	bool ret = false;

	free = 0;
	frees = 987;
	actual = tree_new(free, true, TREE_FIXED, 0);
	expect = tree_new(free + frees, false, TREE_FIXED, 0);
	ret = tree_unreserve(&actual, frees, TREE_FIXED, 0);
	check(ret);
	equal_trees(actual, expect);

	free = 453;
	frees = 987;
	actual = tree_new(free, true, TREE_FIXED, 0);
	expect = tree_new(free + frees, false, TREE_FIXED, 0);
	ret = tree_unreserve(&actual, frees, TREE_FIXED, 0);
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
	treeF_t free;
	bool ret = false;

	order = 0;
	free = 0;
	actual = tree_new(free, false, TREE_FIXED, 0);
	expect = tree_new(free + (treeF_t)(1 << order), false, TREE_FIXED, 0);
	ret = tree_inc(&actual, (treeF_t)(1 << order), false);
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	free = LLFREE_TREE_SIZE -
	       (treeF_t)(1 << order); // max free for success
	actual = tree_new(free, false, TREE_FIXED, 0);
	expect = tree_new(free + (treeF_t)(1 << order), false, TREE_FIXED, 0);
	ret = tree_inc(&actual, (treeF_t)(1 << order), false);
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	free = 3456;
	// should be no difference if reserved is true
	actual = tree_new(free, true, TREE_FIXED, 0);
	expect = tree_new(free + (treeF_t)(1 << order), true, TREE_FIXED, 0);
	ret = tree_inc(&actual, (treeF_t)(1 << order), false);
	check(ret);
	equal_trees(actual, expect);

	order = LLFREE_HUGE_ORDER;
	free = 3456;
	actual = tree_new(free, true, TREE_FIXED, 0);
	expect = tree_new(free + (treeF_t)(1 << order), true, TREE_FIXED, 0);
	ret = tree_inc(&actual, (treeF_t)(1 << order), false);
	check(ret);
	equal_trees(actual, expect);

	order = LLFREE_HUGE_ORDER;
	free = LLFREE_TREE_SIZE - (1 << 9); // max free
	actual = tree_new(free, true, TREE_FIXED, 0);
	expect = tree_new(free + (treeF_t)(1 << order), true, TREE_FIXED, 0);
	ret = tree_inc(&actual, (treeF_t)(1 << order), false);
	check(ret);
	equal_trees(actual, expect);

	return success;
}
