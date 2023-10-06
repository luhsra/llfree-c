#include "check.h"
#include "tree.h"
#include "utils.h"

#define equal_trees(actual, expect)            \
	check_equal(actual.free, expect.free); \
	check_equal(actual.flag, expect.flag)

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
	bool flag = false;

	tree_t actual = tree_new(free, flag);
	check_equal(actual.free, free);
	check_equal(actual.flag, flag);

	free = 0x3fff; // maximum value
	flag = false;
	actual = tree_new(free, flag);
	check_equal(actual.free, free);
	check_equal(actual.flag, flag);

	free = 0; // minimum value
	flag = true; // check if flag is set
	actual = tree_new(free, flag);
	check_equal(actual.free, free);
	check_equal(actual.flag, flag);

	return success;
}

declare_test(tree_reserve)
{
	int success = true;
	bool ret = false;

	tree_t actual = tree_new(7645, false);
	tree_t expect = tree_new(0, true);

	ret = tree_reserve(&actual, 0, (1 << TREE_SHIFT));
	check(ret);
	equal_trees(actual, expect);

	// chek min counter value
	actual = tree_new(0, false);
	expect = tree_new(0, true);
	ret = tree_reserve(&actual, 0, (1 << TREE_SHIFT));
	check(ret);
	equal_trees(actual, expect);

	// if already reserved
	actual = tree_new(456, true);
	expect = actual; // no change expected
	ret = tree_reserve(&actual, 0, (1 << TREE_SHIFT));
	check_m(!ret, "already reserved");
	equal_trees(actual, expect);

	// max counter value
	actual = tree_new(0x3fff, false);
	expect = tree_new(0, true);
	ret = tree_reserve(&actual, 0, (1 << TREE_SHIFT));
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
	frees = 9873;
	actual = tree_new(free, true);
	expect = tree_new(free + frees, false);
	ret = tree_writeback(&actual, frees);
	check(ret);
	equal_trees(actual, expect);

	free = 4532;
	frees = 9873;
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
	ret = tree_inc(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	free = 16383; // max free for success
	actual = tree_new(free, false);
	expect = tree_new(free + (1 << order), false);
	ret = tree_inc(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	free = 3456;
	// should be no difference if flag is true
	actual = tree_new(free, true);
	expect = tree_new(free + (1 << order), true);
	ret = tree_inc(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = HP_ORDER;
	free = 3456;
	actual = tree_new(free, true);
	expect = tree_new(free + (1 << order), true);
	ret = tree_inc(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = HP_ORDER;
	free = (1 << 14) - (1 << 9); // max free
	actual = tree_new(free, true);
	expect = tree_new(free + (1 << order), true);
	ret = tree_inc(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	return success;
}

declare_test(tree_dec)
{
	bool success = true;

	tree_t actual;
	tree_t expect;
	size_t order;
	int free;
	bool ret = false;

	order = 0;
	free = 1 << 14;
	actual = tree_new(free, false);
	expect = tree_new(free - (1 << order), false);
	ret = tree_dec(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	free = 1; // min free for success
	actual = tree_new(free, false);
	expect = tree_new(free - (1 << order), false);
	ret = tree_dec(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	free = 3456;
	// should be no difference if flag is true
	actual = tree_new(free, true);
	expect = tree_new(free - (1 << order), true);
	ret = tree_dec(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = HP_ORDER;
	free = 13370;
	actual = tree_new(free, true);
	expect = tree_new(free - (1 << order), true);
	ret = tree_dec(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = HP_ORDER;
	free = (1 << 9); // min free
	actual = tree_new(free, true);
	expect = tree_new(free - (1 << order), true);
	ret = tree_dec(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	return success;
}
