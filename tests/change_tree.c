#include "test.h"

#include "llfree.h"
#include "llfree_inner.h"
#include "trees.h"

// Helper macros for movable tiering requests
#define ll_cores(self) ll_local_tier_locals((self)->local, 0)
#define llreq(self, core, order) \
	llfree_movable_request(ll_cores(self), (uint8_t)(order), core, false)

static llfree_t llfree_new(size_t cores, size_t frames, uint8_t init)
{
	llfree_t upper;
	llfree_tiering_t tiering = llfree_tiering_movable(cores);
	llfree_meta_size_t m = llfree_metadata_size(&tiering, frames);
	llfree_meta_t meta = {
		.local = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.local),
		.trees = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.trees),
		.lower = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.lower),
	};
	llfree_result_t ll_unused ret =
		llfree_init(&upper, frames, init, meta, &tiering);
	assert(llfree_is_ok(ret));
	return upper;
}

static void llfree_drop(llfree_t *self)
{
	llfree_meta_size_t ms = llfree_metadata_size_of(self);
	llfree_meta_t m = llfree_metadata(self);
	llfree_ext_free(LLFREE_CACHE_SIZE, ms.local, m.local);
	llfree_ext_free(LLFREE_CACHE_SIZE, ms.trees, m.trees);
	llfree_ext_free(LLFREE_CACHE_SIZE, ms.lower, m.lower);
}
#define lldrop __attribute__((cleanup(llfree_drop)))

declare_test(change_tree_promotion_demotion)
{
	bool success = true;
	lldrop llfree_t upper =
		llfree_new(2, 4 * LLFREE_TREE_SIZE, LLFREE_INIT_FREE);

	const size_t tree_id = 0;
	uint8_t tier = 0;
	treeF_t free = 0;
	bool reserved = false;
	trees_stats_at(&upper.trees, tree_id, &tier, &free, &reserved);
	check(!reserved);
	check_equal("u", tier, 2u);
	check_equal("zu", (size_t)free, (size_t)LLFREE_TREE_SIZE);

	llfree_tree_match_t matcher = { .id = ll_some(tree_id),
					.tier = 2,
					.free = LLFREE_TREE_SIZE };
	llfree_tree_change_t demote = { .tier = 0,
					.operation = LLFREE_TREE_OP_NONE };
	llfree_result_t res = llfree_change_tree(&upper, matcher, demote);
	check(llfree_is_ok(res));

	trees_stats_at(&upper.trees, tree_id, &tier, &free, &reserved);
	check_equal("u", tier, 0u);
	check_equal("zu", (size_t)free, (size_t)LLFREE_TREE_SIZE);

	matcher.tier = 0;
	llfree_tree_change_t promote = { .tier = 2,
					 .operation = LLFREE_TREE_OP_NONE };
	res = llfree_change_tree(&upper, matcher, promote);
	check(llfree_is_ok(res));

	trees_stats_at(&upper.trees, tree_id, &tier, &free, &reserved);
	check_equal("u", tier, 2u);
	check_equal("zu", (size_t)free, (size_t)LLFREE_TREE_SIZE);

	llfree_validate(&upper);
	return success;
}

declare_test(change_tree_offline)
{
	bool success = true;
	lldrop llfree_t upper =
		llfree_new(2, 4 * LLFREE_TREE_SIZE, LLFREE_INIT_FREE);
	size_t before = llfree_tree_stats(&upper).free_frames;

	const size_t tree_id = 0;
	llfree_tree_match_t matcher = { .id = ll_some(tree_id),
					.tier = LLFREE_TIER_NONE,
					.free = LLFREE_TREE_SIZE };
	llfree_tree_change_t offline = { .tier = LLFREE_TIER_NONE,
					 .operation = LLFREE_TREE_OP_OFFLINE };

	llfree_result_t res = llfree_change_tree(&upper, matcher, offline);
	check(llfree_is_ok(res));

	uint8_t tier = 0;
	treeF_t free = 0;
	bool reserved = false;
	trees_stats_at(&upper.trees, tree_id, &tier, &free, &reserved);
	check(!reserved);
	check_equal("zu", (size_t)free, 0ul);
	check_equal("zu", llfree_tree_stats(&upper).free_frames,
		    before - LLFREE_TREE_SIZE);

	llfree_result_t at = llfree_get(
		&upper, ll_some(frame_from_tree(tree_id)), llreq(&upper, 0, 0));
	check_m(!llfree_is_ok(at), "offlined tree should reject direct alloc");

	llfree_result_t other = llfree_get(&upper, ll_some(frame_from_tree(1)),
					   llreq(&upper, 0, 0));
	check_m(llfree_is_ok(other), "other trees should still be allocatable");

	return success;
}

declare_test(change_tree_offline_online)
{
	bool success = true;
	lldrop llfree_t upper =
		llfree_new(2, 4 * LLFREE_TREE_SIZE, LLFREE_INIT_FREE);

	const size_t tree_id = 0;
	llfree_tree_match_t matcher = { .id = ll_some(tree_id),
					.tier = LLFREE_TIER_NONE,
					.free = LLFREE_TREE_SIZE };
	llfree_tree_change_t offline = { .tier = LLFREE_TIER_NONE,
					 .operation = LLFREE_TREE_OP_OFFLINE };
	llfree_result_t res = llfree_change_tree(&upper, matcher, offline);
	check(llfree_is_ok(res));

	matcher.free = 0;
	llfree_tree_change_t online = { .tier = LLFREE_TIER_NONE,
					.operation = LLFREE_TREE_OP_ONLINE };
	res = llfree_change_tree(&upper, matcher, online);
	check(llfree_is_ok(res));

	uint8_t tier = 0;
	treeF_t free = 0;
	bool reserved = false;
	trees_stats_at(&upper.trees, tree_id, &tier, &free, &reserved);
	check(!reserved);
	check_equal("zu", (size_t)free, (size_t)LLFREE_TREE_SIZE);

	llfree_result_t at = llfree_get(
		&upper, ll_some(frame_from_tree(tree_id)), llreq(&upper, 0, 0));
	check_m(llfree_is_ok(at), "onlined tree should allow direct alloc");

	llfree_validate(&upper);
	return success;
}
