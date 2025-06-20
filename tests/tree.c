#include "check.h"
#include "tree.h"

#define equal_trees(actual, expect)                 \
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

	tree_t actual = tree_new(reserved, TREE_FIXED, free, 0);
	check_equal("u", actual.free, free);
	check_equal("u", actual.reserved, reserved);

	free = LLFREE_TREE_SIZE; // maximum value
	reserved = false;
	actual = tree_new(reserved, TREE_FIXED, free, 0);
	check_equal("u", actual.free, free);
	check_equal("u", actual.reserved, reserved);

	free = 0; // minimum value
	reserved = true; // check if reserved is set
	actual = tree_new(reserved, TREE_FIXED, free, 0);
	check_equal("u", actual.free, free);
	check_equal("u", actual.reserved, reserved);

	return success;
}

declare_test(tree_reserve)
{
	int success = true;
	bool ret = false;

	tree_t actual = tree_new(false, TREE_FIXED, 764, 0);
	tree_t expect = tree_new(true, TREE_FIXED, 0, 0);

	tree_change_t change = tree_change_small(0, false);
	ret = tree_reserve(&actual, change, (1 << LLFREE_TREE_ORDER));
	check(ret);
	equal_trees(actual, expect);

	// check min counter value
	actual = tree_new(false, TREE_FIXED, 0, 0);
	expect = tree_new(true, TREE_FIXED, 0, 0);
	ret = tree_reserve(&actual, change, (1 << LLFREE_TREE_ORDER));
	check(ret);
	equal_trees(actual, expect);

	// if already reserved
	actual = tree_new(true, TREE_FIXED, 456, 0);
	expect = actual; // no change expected
	ret = tree_reserve(&actual, change, (1 << LLFREE_TREE_ORDER));
	check_m(!ret, "already reserved");
	equal_trees(actual, expect);

	// max counter value
	actual = tree_new(false, TREE_FIXED, LLFREE_TREE_SIZE, 0);
	expect = tree_new(true, TREE_FIXED, 0, 0);
	ret = tree_reserve(&actual, change, (1 << LLFREE_TREE_ORDER));
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
	actual = tree_new(true, TREE_FIXED, free, 0);
	expect = tree_new(false, TREE_FIXED, free + frees, 0);
	ret = tree_unreserve(&actual, tree_change_small(frees, false));
	check(ret);
	equal_trees(actual, expect);

	free = 453;
	frees = 987;
	actual = tree_new(true, TREE_FIXED, free, 0);
	expect = tree_new(false, TREE_FIXED, free + frees, 0);
	ret = tree_unreserve(&actual, tree_change_small(frees, false));
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
	actual = tree_new(false, TREE_FIXED, free, 0);
	expect = tree_new(false, TREE_FIXED, free + (treeF_t)(1 << order), 0);
	ret = tree_put(&actual, tree_change_small((treeF_t)(1 << order), false));
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	free = LLFREE_TREE_SIZE - (treeF_t)(1 << order); // max free for success
	actual = tree_new(false, TREE_FIXED, free, 0);
	expect = tree_new(false, TREE_FIXED, free + (treeF_t)(1 << order), 0);
	ret = tree_put(&actual, tree_change_small((treeF_t)(1 << order), false));
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	free = 3456;
	// should be no difference if reserved is true
	actual = tree_new(true, TREE_FIXED, free, 0);
	expect = tree_new(true, TREE_FIXED, free + (treeF_t)(1 << order), 0);
	ret = tree_put(&actual, tree_change_small((treeF_t)(1 << order), false));
	check(ret);
	equal_trees(actual, expect);

	order = LLFREE_HUGE_ORDER;
	free = 3456;
	actual = tree_new(true, TREE_FIXED, free, 0);
	expect = tree_new(true, TREE_FIXED, free + (treeF_t)(1 << order), 0);
	ret = tree_put(&actual, tree_change_small((treeF_t)(1 << order), false));
	check(ret);
	equal_trees(actual, expect);

	order = LLFREE_HUGE_ORDER;
	free = LLFREE_TREE_SIZE - (1 << 9); // max free
	actual = tree_new(true, TREE_HUGE, free, 0);
	expect = tree_new(true, TREE_HUGE, free + (treeF_t)(1 << order), 0);
	ret = tree_put(&actual, tree_change_huge(1, false));
	check(ret);
	equal_trees(actual, expect);

	return success;
}
