#include "check.h"
#include "tree.h"
#include "utils.h"

#define equal_trees(actual, expect)                  \
	check_equal(actual.counter, expect.counter); \
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

	int counter = 498;
	bool flag = false;

	tree_t actual = tree_new(counter, flag);
	check_equal(actual.counter, counter);
	check_equal(actual.flag, flag);

	counter = 0x3fff; //maximum value
	flag = false;
	actual = tree_new(counter, flag);
	check_equal(actual.counter, counter);
	check_equal(actual.flag, flag);

	counter = 0; //minimum value
	flag = true; //check if flag is set
	actual = tree_new(counter, flag);
	check_equal(actual.counter, counter);
	check_equal(actual.flag, flag);

	return success;
}

declare_test(tree_reserve)
{
	int success = true;
	bool ret = false;

	tree_t actual = tree_new(7645, false);
	tree_t expect = tree_new(0, true);

	ret = tree_reserve(&actual, (range_t){ 0, (1 << TREE_SHIFT) });
	check(ret);
	equal_trees(actual, expect);

	// chek min counter value
	actual = tree_new(0, false);
	expect = tree_new(0, true);
	ret = tree_reserve(&actual, (range_t){ 0, (1 << TREE_SHIFT) });
	check(ret);
	equal_trees(actual, expect);

	// if already reserved
	actual = tree_new(456, true);
	expect = actual; // no change expected
	ret = tree_reserve(&actual, (range_t){ 0, (1 << TREE_SHIFT) });
	check_m(!ret, "already reserved");
	equal_trees(actual, expect);

	// max counter value
	actual = tree_new(0x3fff, false);
	expect = tree_new(0, true);
	ret = tree_reserve(&actual, (range_t){ 0, (1 << TREE_SHIFT) });
	check(ret);
	equal_trees(actual, expect);

	return success;
}

declare_test(tree_unreserve)
{
	int success = true;

	int counter;
	int frees;
	tree_t actual;
	tree_t expect;
	bool ret = false;

	counter = 0;
	frees = 9873;
	actual = tree_new(counter, true);
	expect = tree_new(counter + frees, false);
	ret = tree_writeback(&actual, frees);
	check(ret);
	equal_trees(actual, expect);

	counter = 4532;
	frees = 9873;
	actual = tree_new(counter, true);
	expect = tree_new(counter + frees, false);
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
	int counter;
	bool ret = false;

	order = 0;
	counter = 0;
	actual = tree_new(counter, false);
	expect = tree_new(counter + (1 << order), false);
	ret = tree_counter_inc(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	counter = 16383; //max counter for success
	actual = tree_new(counter, false);
	expect = tree_new(counter + (1 << order), false);
	ret = tree_counter_inc(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	counter = 3456;
	//should be no difference if flag is true
	actual = tree_new(counter, true);
	expect = tree_new(counter + (1 << order), true);
	ret = tree_counter_inc(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = HP_ORDER;
	counter = 3456;
	actual = tree_new(counter, true);
	expect = tree_new(counter + (1 << order), true);
	ret = tree_counter_inc(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = HP_ORDER;
	counter = (1 << 14) - (1 << 9); // max counter
	actual = tree_new(counter, true);
	expect = tree_new(counter + (1 << order), true);
	ret = tree_counter_inc(&actual, order);
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
	int counter;
	bool ret = false;

	order = 0;
	counter = 1 << 14;
	actual = tree_new(counter, false);
	expect = tree_new(counter - (1 << order), false);
	ret = tree_counter_dec(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	counter = 1; // min counter for success
	actual = tree_new(counter, false);
	expect = tree_new(counter - (1 << order), false);
	ret = tree_counter_dec(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	counter = 3456;
	// should be no difference if flag is true
	actual = tree_new(counter, true);
	expect = tree_new(counter - (1 << order), true);
	ret = tree_counter_dec(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = HP_ORDER;
	counter = 13370;
	actual = tree_new(counter, true);
	expect = tree_new(counter - (1 << order), true);
	ret = tree_counter_dec(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	order = HP_ORDER;
	counter = (1 << 9); // min counter
	actual = tree_new(counter, true);
	expect = tree_new(counter - (1 << order), true);
	ret = tree_counter_dec(&actual, order);
	check(ret);
	equal_trees(actual, expect);

	return success;
}
