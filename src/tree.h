#pragma once

#include "llfree.h"
#include "utils.h"

typedef uint32_t treeF_t;
/// Number of bits for the free counter in tree_t (32 - 1 reserved - 3 class = 28)
#define LLFREE_TREE_FREE_BITS ((8 * sizeof(treeF_t)) - 1 - LLFREE_CLASS_BITS)
_Static_assert((1u << LLFREE_TREE_FREE_BITS) > LLFREE_TREE_SIZE,
	       "Tree free counter too small");

/// Tree entry: tracks free frames and the class for a subtree
typedef struct tree {
	/// Whether this tree is reserved by a CPU.
	bool reserved : 1;
	/// The class of pages this tree primarily contains.
	/// Class 0 = immovable small, 1 = movable small, N-1 = huge.
	uint8_t class : LLFREE_CLASS_BITS;
	/// Number of free frames in this tree.
	treeF_t free : LLFREE_TREE_FREE_BITS;
} tree_t;
_Static_assert(sizeof(tree_t) == sizeof(treeF_t), "tree size mismatch");

/// Lower bound used by the tree search heuristics
#define TREE_LOWER_LIM (LLFREE_TREE_SIZE / 16)

/// Create a new tree entry
static inline ll_unused tree_t tree_new(bool reserved, uint8_t class,
					treeF_t free)
{
	assert(free <= LLFREE_TREE_SIZE);
	assert(class < LLFREE_MAX_CLASSES);
	return (tree_t){ .reserved = reserved, .class = class, .free = free };
}

/// Return frames to a tree (increment free counter).
/// Resets class to default_class when tree becomes entirely free.
bool tree_put(tree_t *self, treeF_t frames, llfree_policy_fn policy,
	      uint8_t default_class);

/// Steal frames from a tree (decrement free counter).
/// Returns true on success, false if tree has insufficient free or its class is incompatible.
bool tree_steal(tree_t *self, treeF_t frames, uint8_t *class,
		llfree_policy_fn policy);

/// Reserve an entire tree (Match/Demote) or decrement its counter (Steal).
/// On Match or Demote: sets reserved=true, free=0, class=requested class.
/// On Steal: decrements free counter, keeps existing class.
/// Returns true on success, false if tree is already reserved or has insufficient free.
/// *out_reserved: true if tree was reserved, false if stolen.
/// *out_class: the resulting class (requested for reserve, existing for steal).
bool tree_reserve_or_steal(tree_t *self, treeF_t frames,
			   llfree_policy_fn policy, uint8_t class,
			   bool *out_reserved, uint8_t *out_class);

/// Unreserve a tree and add frames back; optionally demotes class via policy.
/// Resets class to default_class when tree becomes entirely free.
bool tree_unreserve_add(tree_t *self, treeF_t frames, uint8_t class,
			llfree_policy_fn policy, uint8_t default_class);

/// Steal free counter from a reserved tree (sets free=0).
/// Returns true if reserved and free > min.
bool tree_sync_steal(tree_t *self, treeF_t min);

/// Change a tree entry if matcher conditions are met.
/// Returns false if it does not match or if operation preconditions fail.
bool tree_change(tree_t *self, uint8_t match_class, treeF_t min_free,
		 uint8_t change_class, llfree_tree_operation_t operation,
		 treeF_t online_free);

/// Debug print the tree
void tree_print(tree_t *self, tree_id_t idx, size_t indent);
