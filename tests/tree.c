#include "test.h"
#include "tree.h"

#define equal_trees(actual, expect)                 \
	check_equal("u", actual.free, expect.free); \
	check_equal("u", actual.reserved, expect.reserved)

// Simple policy: cluster 0 = small, cluster 1 = huge; same cluster matches only
static llfree_policy_t test_policy(uint8_t req, uint8_t tgt, size_t free)
{
	(void)free;
	if (req == tgt)
		return (llfree_policy_t){ LLFREE_POLICY_MATCH, 255 };
	if (req < tgt)
		return (llfree_policy_t){ LLFREE_POLICY_STEAL, 0 };
	return (llfree_policy_t){ LLFREE_POLICY_DEMOTE, 0 };
}



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

	tree_t actual = tree_new(reserved, 0, free);
	check_equal("u", actual.free, free);
	check_equal("u", actual.reserved, reserved);
	check_equal("u", actual.cluster, 0);

	free = LLFREE_TREE_SIZE; // maximum value
	reserved = false;
	actual = tree_new(reserved, 0, free);
	check_equal("u", actual.free, free);
	check_equal("u", actual.reserved, reserved);

	free = 0; // minimum value
	reserved = true;
	actual = tree_new(reserved, 0, free);
	check_equal("u", actual.free, free);
	check_equal("u", actual.reserved, reserved);

	return success;
}

declare_test(tree_reserve)
{
	int success = true;
	bool ret = false;
	bool reserved = false;
	uint8_t out_cluster = 0;

	// Use test_policy: same cluster -> Match -> reserve behavior
	// tree_reserve_or_steal with Match sets reserved=true, free=0, cluster=req

	tree_t actual = tree_new(false, 0, 764);
	tree_t expect = tree_new(true, 0, 0);

	ret = tree_reserve_or_steal(&actual, 1, test_policy, 0, &reserved, &out_cluster);
	check(ret);
	check(reserved);
	equal_trees(actual, expect);

	// if already reserved
	actual = tree_new(true, 0, 456);
	expect = actual;
	ret = tree_reserve_or_steal(&actual, 1, test_policy, 0, &reserved, &out_cluster);
	check_m(!ret, "already reserved");
	equal_trees(actual, expect);

	// max counter value
	actual = tree_new(false, 0, LLFREE_TREE_SIZE);
	expect = tree_new(true, 0, 0);
	ret = tree_reserve_or_steal(&actual, 1, test_policy, 0, &reserved, &out_cluster);
	check(ret);
	check(reserved);
	equal_trees(actual, expect);

	// Steal behavior: target > request -> Steal, decrements counter
	actual = tree_new(false, 1, 764);
	treeF_t free_before = actual.free;
	ret = tree_reserve_or_steal(&actual, 4, test_policy, 0, &reserved, &out_cluster);
	check(ret);
	check(!reserved);
	check_equal("u", actual.free, free_before - 4);
	check_equal("u", actual.cluster, 1); // cluster unchanged

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
	actual = tree_new(true, 0, free);
	expect = tree_new(false, 0, free + frees);
	ret = tree_unreserve_add(&actual, frees, 0, test_policy, 0);
	check(ret);
	equal_trees(actual, expect);

	free = 453;
	frees = 987;
	actual = tree_new(true, 0, free);
	expect = tree_new(false, 0, free + frees);
	ret = tree_unreserve_add(&actual, frees, 0, test_policy, 0);
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
	actual = tree_new(false, 0, free);
	expect = tree_new(false, 0, free + (treeF_t)(1 << order));
	ret = tree_put(&actual, (treeF_t)(1 << order), test_policy, 0);
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	free = LLFREE_TREE_SIZE - (treeF_t)(1 << order);
	actual = tree_new(false, 0, free);
	expect = tree_new(false, 0, free + (treeF_t)(1 << order));
	ret = tree_put(&actual, (treeF_t)(1 << order), test_policy, 0);
	check(ret);
	equal_trees(actual, expect);

	order = 0;
	free = 3456;
	// reserved flag should not matter for put
	actual = tree_new(true, 0, free);
	expect = tree_new(true, 0, free + (treeF_t)(1 << order));
	ret = tree_put(&actual, (treeF_t)(1 << order), test_policy, 0);
	check(ret);
	equal_trees(actual, expect);

	order = LLFREE_HUGE_ORDER;
	free = 3456;
	actual = tree_new(true, 0, free);
	expect = tree_new(true, 0, free + (treeF_t)(1 << order));
	ret = tree_put(&actual, (treeF_t)(1 << order), test_policy, 0);
	check(ret);
	equal_trees(actual, expect);

	order = LLFREE_HUGE_ORDER;
	free = LLFREE_TREE_SIZE - (1 << 9);
	actual = tree_new(true, 1, free); // cluster 1 = huge
	// When tree becomes entirely free, cluster resets to default_cluster (0)
	expect = tree_new(true, 0, free + (treeF_t)(1 << order));
	ret = tree_put(&actual, (treeF_t)(1 << order), test_policy, 0);
	check(ret);
	equal_trees(actual, expect);

	return success;
}
