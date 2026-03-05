#pragma once

#include "llfree.h"
#include "utils.h"

typedef uint32_t treeF_t;
#define LLFREE_TREE_FREE_BITS ((sizeof(treeF_t) * 8) - 1 - LLKIND_BITS)
_Static_assert(LLFREE_TREE_FREE_BITS > LLFREE_TREE_ORDER, "Tree free counter");

/// Tree entry
typedef struct tree {
	/// Whether this tree has been reserved.
	bool reserved : 1;
	/// The kind of pages this tree contains.
	uint8_t kind : LLKIND_BITS;
	/// Number of free frames in this tree.
	/// If TREE_HUGE, this has to be a multiple of LLFREE_CHILD_SIZE.
	treeF_t free : LLFREE_TREE_FREE_BITS;
} tree_t;
_Static_assert(sizeof(tree_t) == sizeof(treeF_t), "tree size mismatch");

typedef struct p_range {
	treeF_t min, max;
} p_range_t;

/// Lower bound used by the tree search heuristics
#define TREE_LOWER_LIM (LLFREE_TREE_SIZE / 16)

/// Create a new tree entry
static inline ll_unused tree_t tree_new(bool reserved, llkind_t kind,
					treeF_t free)
{
	assert(free <= LLFREE_TREE_SIZE);
	assert(kind.id < LLKIND_HUGE.id || (free % LLFREE_CHILD_SIZE) == 0);
	assert(kind.id <= LLKIND_ZERO.id);
	// Correct zeroed count
	return (tree_t){ .reserved = reserved, .kind = kind.id, .free = free };
}

/// Return frames to a tree.
/// - This might promote the tree to a higher kind.
bool tree_put(tree_t *self, treeF_t free);

/// Allocate frames from a tree if the kind matches.
bool tree_get(tree_t *self, treeF_t free, llkind_t kind);

/// Allocate frames from a tree, demoting it if the kind does not match.
bool tree_get_demote(tree_t *self, treeF_t free, llkind_t kind);

/// Reserves a tree if the change would succeed.
bool tree_reserve(tree_t *self, treeF_t free, treeF_t max, llkind_t kind);

/// Adds the given counter and clears reserved
bool tree_unreserve(tree_t *self, treeF_t free, llkind_t kind);

// Steals the counter of a reserved tree
bool tree_steal_counter(tree_t *self, treeF_t free);

/// Increment the free counter or reserve if specified
bool tree_put_or_reserve(tree_t *self, treeF_t free, llkind_t kind,
			 bool *reserve, treeF_t min);

bool tree_demote(tree_t *self, llkind_t kind);

/// Try reclaiming a huge page from the tree.
bool tree_reclaim(tree_t *self, bool *success, bool not_zeroed, bool alloc);
bool tree_undo_reclaim(tree_t *self, bool not_zeroed, bool alloc);

/// Debug print the tree
void tree_print(tree_t *self, size_t idx, size_t indent);
