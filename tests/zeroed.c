#include "llfree.h"
#include "llfree_inner.h"

#include "test.h"

// Tiers used by these tests:
// 0 = small, 1 = huge (non-zeroed), 2 = huge (zeroed)
enum : uint8_t {
	ZEROED_TIER_SMALL = 0,
	ZEROED_TIER_HUGE = 1,
	ZEROED_TIER_HUGE_ZEROED = 2,
};

#define ll_cores(self) ll_local_tier_locals((self)->local, 0)

static llfree_policy_t zeroed_policy(uint8_t requested, uint8_t target,
				     size_t free)
{
	if (requested > target)
		return (llfree_policy_t){ LLFREE_POLICY_STEAL, 0 };
	if (requested < target)
		return (llfree_policy_t){ LLFREE_POLICY_DEMOTE, 0 };
	if (free >= LLFREE_TREE_SIZE / 2)
		return (llfree_policy_t){ LLFREE_POLICY_MATCH, 1 };
	if (free >= LLFREE_TREE_SIZE / 64)
		return (llfree_policy_t){ LLFREE_POLICY_MATCH, UINT8_MAX };
	return (llfree_policy_t){ LLFREE_POLICY_MATCH, 0 };
}

static llfree_tiering_t zeroed_tiering(size_t cores)
{
	llfree_tiering_t t = { .num_tiers = 3,
			       .default_tier = ZEROED_TIER_HUGE,
			       .policy = zeroed_policy };
	t.tiers[0] =
		(llfree_tier_conf_t){ .tier = ZEROED_TIER_SMALL, .count = cores };
	t.tiers[1] =
		(llfree_tier_conf_t){ .tier = ZEROED_TIER_HUGE, .count = cores };
	t.tiers[2] = (llfree_tier_conf_t){ .tier = ZEROED_TIER_HUGE_ZEROED,
					   .count = cores };
	return t;
}

static llfree_request_t req_small(llfree_t *self, size_t core)
{
	return llreq(0, ZEROED_TIER_SMALL, core % ll_cores(self));
}

static llfree_request_t req_zeroed_huge(llfree_t *self, size_t core)
{
	return llreq(LLFREE_HUGE_ORDER, ZEROED_TIER_HUGE_ZEROED,
		     core % ll_cores(self));
}

static size_t tier_free_frames(const llfree_t *self, uint8_t tier)
{
	return llfree_tree_stats(self).tiers[tier].free_frames;
}

static llfree_t llfree_new(size_t cores, size_t frames, uint8_t init)
{
	llfree_t upper;
	llfree_tiering_t tiering = zeroed_tiering(cores);
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

// Convert one fully free huge-tier tree into zeroed-huge tier.
// Uses matcher without tree id to model background "find any free tree".
static llfree_result_t convert_any_free_huge_tree(llfree_t *self)
{
	llfree_tree_match_t matcher = {
		.id = ll_none(),
		.tier = ZEROED_TIER_HUGE,
		.free = LLFREE_TREE_SIZE,
	};
	llfree_tree_change_t offline = {
		.tier = LLFREE_TIER_NONE,
		.operation = LLFREE_TREE_OP_OFFLINE,
	};
	llfree_result_t res = llfree_change_tree(self, matcher, offline);
	if (!llfree_is_ok(res))
		return res;

	// Zeroing happens here...

	llfree_tree_change_t online_zeroed = {
		.tier = ZEROED_TIER_HUGE_ZEROED,
		.operation = LLFREE_TREE_OP_ONLINE,
	};
	matcher.free = 0;
	return llfree_change_tree(self, matcher, online_zeroed);
}

declare_test(zeroed_prefers_tier)
{
	bool success = true;
	const size_t FRAMES = 12 * LLFREE_TREE_SIZE;
	lldrop llfree_t upper = llfree_new(2, FRAMES, LLFREE_INIT_FREE);

	llfree_validate(&upper);
	check_equal("zu", llfree_frames(&upper), FRAMES);
	check_equal("zu", tier_free_frames(&upper, ZEROED_TIER_HUGE), FRAMES);
	check_equal("zu", tier_free_frames(&upper, ZEROED_TIER_HUGE_ZEROED), 0ul);

	llfree_result_t conv = convert_any_free_huge_tree(&upper);
	check_m(llfree_is_ok(conv),
		"should find any fully free huge tree to convert");
	check_equal("zu", tier_free_frames(&upper, ZEROED_TIER_HUGE),
		    FRAMES - LLFREE_TREE_SIZE);
	check_equal("zu", tier_free_frames(&upper, ZEROED_TIER_HUGE_ZEROED),
		    (size_t)LLFREE_TREE_SIZE);

	llfree_result_t res = llfree_get(&upper, ll_none(), req_zeroed_huge(&upper, 0));
	check(llfree_is_ok(res));
	check_equal("u", res.tier, (unsigned)ZEROED_TIER_HUGE_ZEROED);

	llfree_validate(&upper);
	return success;
}

declare_test(zeroed_steals_from_huge)
{
	bool success = true;
	const size_t FRAMES = 12 * LLFREE_TREE_SIZE;
	lldrop llfree_t upper = llfree_new(2, FRAMES, LLFREE_INIT_FREE);

	llfree_validate(&upper);
	check_equal("zu", tier_free_frames(&upper, ZEROED_TIER_HUGE), FRAMES);
	check_equal("zu", tier_free_frames(&upper, ZEROED_TIER_HUGE_ZEROED), 0ul);

	llfree_result_t conv = convert_any_free_huge_tree(&upper);
	check(llfree_is_ok(conv));
	check_equal("zu", tier_free_frames(&upper, ZEROED_TIER_HUGE),
		    FRAMES - LLFREE_TREE_SIZE);
	check_equal("zu", tier_free_frames(&upper, ZEROED_TIER_HUGE_ZEROED),
		    (size_t)LLFREE_TREE_SIZE);

	// A converted tree provides exactly LLFREE_TREE_CHILDREN huge frames.
	const size_t zeroed_huge_capacity = LLFREE_TREE_CHILDREN;
	for (size_t i = 0; i < zeroed_huge_capacity; i++) {
		llfree_result_t r =
			llfree_get(&upper, ll_none(), req_zeroed_huge(&upper, 0));
		check(llfree_is_ok(r));
		check_equal("u", r.tier,
			    (unsigned)ZEROED_TIER_HUGE_ZEROED);
	}

	// Next zeroed request should fallback by stealing from huge tier.
	llfree_result_t fb =
		llfree_get(&upper, ll_none(), req_zeroed_huge(&upper, 0));
	check(llfree_is_ok(fb));
	check_equal("u", fb.tier, (unsigned)ZEROED_TIER_HUGE);

	llfree_validate(&upper);
	check_equal("zu", tier_free_frames(&upper, ZEROED_TIER_HUGE_ZEROED),
		    LLFREE_TREE_SIZE - (zeroed_huge_capacity << LLFREE_HUGE_ORDER));
	return success;
}

declare_test(zeroed_convert_until_exhausted)
{
	bool success = true;
	const size_t FRAMES = 12 * LLFREE_TREE_SIZE;
	lldrop llfree_t upper = llfree_new(2, FRAMES, LLFREE_INIT_FREE);

	llfree_validate(&upper);

	// Keep converting "any" free huge tree; stop when none remain.
	size_t converted = 0;
	while (true) {
		size_t huge_before = tier_free_frames(&upper, ZEROED_TIER_HUGE);
		size_t zeroed_before =
			tier_free_frames(&upper, ZEROED_TIER_HUGE_ZEROED);
		llfree_result_t r = convert_any_free_huge_tree(&upper);
		if (!llfree_is_ok(r)) {
			check_equal("u", r.error, (unsigned)LLFREE_ERR_MEMORY);
			break;
		}
		check_equal("zu", tier_free_frames(&upper, ZEROED_TIER_HUGE),
			    huge_before - LLFREE_TREE_SIZE);
		check_equal("zu", tier_free_frames(&upper, ZEROED_TIER_HUGE_ZEROED),
			    zeroed_before + LLFREE_TREE_SIZE);
		converted++;
	}

	check_m(converted > 0, "at least one tree should be converted");
	check_equal("zu", tier_free_frames(&upper, ZEROED_TIER_HUGE), 0ul);
	check_equal("zu", tier_free_frames(&upper, ZEROED_TIER_HUGE_ZEROED),
		    FRAMES);

	// With no fully-free huge trees left, conversion must fail.
	llfree_result_t again = convert_any_free_huge_tree(&upper);
	check(!llfree_is_ok(again));
	check_equal("u", again.error, (unsigned)LLFREE_ERR_MEMORY);

	// Zeroed huge allocations should still succeed from zeroed tier.
	llfree_result_t res = llfree_get(&upper, ll_none(), req_zeroed_huge(&upper, 1));
	check(llfree_is_ok(res));
	check_equal("u", res.tier, (unsigned)ZEROED_TIER_HUGE_ZEROED);

	// Small allocation path still works in this tiering setup.
	llfree_result_t small = llfree_get(&upper, ll_none(), req_small(&upper, 0));
	check(llfree_is_ok(small));
	check_equal("u", small.tier, (unsigned)ZEROED_TIER_SMALL);

	llfree_validate(&upper);
	return success;
}
