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

/// Range for tree free-counter search
typedef struct p_range {
	treeF_t min, max;
} p_range_t;

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
bool tree_put(tree_t *self, treeF_t frames);

/// Allocate frames from a tree if tier matches and enough free frames.
bool tree_get(tree_t *self, uint8_t tier, treeF_t frames);

/// Allocate frames, possibly demoting the tier via policy.
bool tree_get_demote(tree_t *self, uint8_t tier, treeF_t frames,
		     llfree_policy_fn policy);

/// Reserve a tree if its free counter is in [min, max] and tier matches
/// (or tree is entirely free). Sets free=0, reserved=true.
bool tree_reserve(tree_t *self, uint8_t tier, treeF_t min, treeF_t max);

/// Unreserve a tree and add frames back; optionally demotes tier via policy.
bool tree_unreserve_add(tree_t *self, treeF_t frames, uint8_t tier,
			llfree_policy_fn policy);

/// Steal free counter from a reserved tree (sets free=0).
/// Returns true if reserved and free > min.
bool tree_sync_steal(tree_t *self, treeF_t min);

/// Increment free counter or reserve for free-reserve heuristic.
bool tree_put_or_reserve(tree_t *self, treeF_t frames, uint8_t tier,
			 bool *reserve, treeF_t min);

/// Atomically change a reserved tree's tier (for steal-demote).
/// Returns true if the tree is reserved and its tier matches from_tier.
bool tree_change_tier(tree_t *self, uint8_t from_tier, uint8_t to_tier);

/// Debug print the tree
void tree_print(tree_t *self, size_t idx, size_t indent);
