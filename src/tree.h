#pragma once

#include "llfree.h"
#include "utils.h"

typedef uint32_t treeF_t;
/// Number of bits for the free counter in tree_t (32 - 1 reserved - 3 tier = 28)
#define LLFREE_TREE_FREE_BITS ((8 * sizeof(treeF_t)) - 1 - LLFREE_TIER_BITS)
_Static_assert((1u << LLFREE_TREE_FREE_BITS) > LLFREE_TREE_SIZE,
	       "Tree free counter too small");

/// Tree entry: tracks free frames and the tier for a subtree
typedef struct tree {
	/// Whether this tree is reserved by a CPU.
	bool reserved : 1;
	/// The tier of pages this tree primarily contains.
	/// Tier 0 = immovable small, 1 = movable small, N-1 = huge.
	uint8_t tier : LLFREE_TIER_BITS;
	/// Number of free frames in this tree.
	treeF_t free : LLFREE_TREE_FREE_BITS;
} tree_t;
_Static_assert(sizeof(tree_t) == sizeof(treeF_t), "tree size mismatch");

/// Check function that returns the resulting tier if successful or LLFREE_TIER_NONE if not.
typedef uint8_t (*tree_check_fn)(uint8_t tree_tier, treeF_t frames, void *args);

/// Range for tree free-counter search
typedef struct p_range {
	treeF_t min, max;
} p_range_t;
static inline ll_unused p_range_t p_range(treeF_t min, treeF_t max)
{
	assert(min <= max);
	return (p_range_t){ min, max };
}

/// Lower bound used by the tree search heuristics
#define TREE_LOWER_LIM (LLFREE_TREE_SIZE / 16)

/// Create a new tree entry
static inline ll_unused tree_t tree_new(bool reserved, uint8_t tier,
					treeF_t free)
{
	assert(free <= LLFREE_TREE_SIZE);
	assert(tier < LLFREE_MAX_TIERS);
	return (tree_t){ .reserved = reserved, .tier = tier, .free = free };
}

/// Return frames to a tree (increment free counter).
/// Resets tier to default_tier when tree becomes entirely free.
bool tree_put(tree_t *self, treeF_t frames, uint8_t default_tier);

bool tree_get(tree_t *self, treeF_t frames, uint8_t *result_tier,
	      tree_check_fn check, void *args);

/// Reserve a tree if check returns a valid tier, and set it to that tier.
bool tree_reserve(tree_t *self, uint8_t *result_tier, tree_check_fn check,
		  void *args);

/// Unreserve a tree and add frames back; optionally demotes tier via policy.
/// Resets tier to default_tier when tree becomes entirely free.
bool tree_unreserve_add(tree_t *self, treeF_t frames, uint8_t tier,
			llfree_policy_fn policy, uint8_t default_tier);

/// Steal free counter from a reserved tree (sets free=0).
/// Returns true if reserved and free > min.
bool tree_sync_steal(tree_t *self, treeF_t min);

/// Increment free counter or reserve for free-reserve heuristic.
/// Resets tier to default_tier when tree becomes entirely free.
bool tree_put_or_reserve(tree_t *self, treeF_t frames, uint8_t tier,
			 bool *reserve, treeF_t min, uint8_t default_tier);

/// Change a tree entry if matcher conditions are met.
/// Returns false if it does not match or if operation preconditions fail.
bool tree_change(tree_t *self, uint8_t match_tier, treeF_t min_free,
		uint8_t change_tier, llfree_tree_operation_t operation,
		treeF_t online_free);

/// Debug print the tree
void tree_print(tree_t *self, size_t idx, size_t indent);
