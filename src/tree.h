#pragma once

#include "llfree.h"
#include "utils.h"

typedef uint32_t treeF_t;
/// Number of bits for the free counter in tree_t (32 - 1 reserved - 3 cluster = 28)
#define LLFREE_TREE_FREE_BITS ((8 * sizeof(treeF_t)) - 1 - LLFREE_CLUSTER_BITS)
_Static_assert((1u << LLFREE_TREE_FREE_BITS) > LLFREE_TREE_SIZE,
	       "Tree free counter too small");

/// Tree entry: tracks free frames and the cluster for a subtree
typedef struct tree {
	/// Whether this tree is reserved by a CPU.
	bool reserved : 1;
	/// The cluster of pages this tree primarily contains.
	/// Cluster 0 = immovable small, 1 = movable small, N-1 = huge.
	uint8_t cluster : LLFREE_CLUSTER_BITS;
	/// Number of free frames in this tree.
	treeF_t free : LLFREE_TREE_FREE_BITS;
} tree_t;
_Static_assert(sizeof(tree_t) == sizeof(treeF_t), "tree size mismatch");

/// Lower bound used by the tree search heuristics
#define TREE_LOWER_LIM (LLFREE_TREE_SIZE / 16)

/// Create a new tree entry
static inline ll_unused tree_t tree_new(bool reserved, uint8_t cluster,
					treeF_t free)
{
	assert(free <= LLFREE_TREE_SIZE);
	assert(cluster < LLFREE_MAX_CLUSTERS);
	return (tree_t){ .reserved = reserved, .cluster = cluster, .free = free };
}

/// Return frames to a tree (increment free counter).
/// Resets cluster to default_cluster when tree becomes entirely free.
bool tree_put(tree_t *self, treeF_t frames, llfree_policy_fn policy,
	      uint8_t default_cluster);

/// Steal frames from a tree (decrement free counter).
/// Returns true on success, false if tree has insufficient free or its cluster is incompatible.
bool tree_steal(tree_t *self, treeF_t frames, uint8_t *cluster,
		llfree_policy_fn policy);

/// Reserve an entire tree (Match/Demote) or decrement its counter (Steal).
/// On Match or Demote: sets reserved=true, free=0, cluster=requested cluster.
/// On Steal: decrements free counter, keeps existing cluster.
/// Returns true on success, false if tree is already reserved or has insufficient free.
/// *out_reserved: true if tree was reserved, false if stolen.
/// *out_cluster: the resulting cluster (requested for reserve, existing for steal).
bool tree_reserve_or_steal(tree_t *self, treeF_t frames,
			   llfree_policy_fn policy, uint8_t cluster,
			   bool *out_reserved, uint8_t *out_cluster);

/// Unreserve a tree and add frames back; optionally demotes cluster via policy.
/// Resets cluster to default_cluster when tree becomes entirely free.
bool tree_unreserve_add(tree_t *self, treeF_t frames, uint8_t cluster,
			llfree_policy_fn policy, uint8_t default_cluster);

/// Steal free counter from a reserved tree (sets free=0).
/// Returns true if reserved and free > min.
bool tree_sync_steal(tree_t *self, treeF_t min);

/// Change a tree entry if matcher conditions are met.
/// Returns false if it does not match or if operation preconditions fail.
bool tree_change(tree_t *self, uint8_t match_cluster, treeF_t min_free,
		 uint8_t change_cluster, llfree_tree_operation_t operation,
		 treeF_t online_free);

/// Debug print the tree
void tree_print(tree_t *self, tree_id_t idx, size_t indent);
