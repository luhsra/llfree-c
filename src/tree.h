#pragma once

#include "utils.h"

typedef uint32_t treeF_t;
#define LLFREE_TREE_FREE_BITS \
	((sizeof(treeF_t) * 8) - 1 - 2 - (LLFREE_TREE_CHILDREN_ORDER + 1))
_Static_assert(LLFREE_TREE_FREE_BITS > LLFREE_TREE_ORDER, "Tree free counter");

/// Tree entry
typedef struct tree {
	/// Number of free frames in this tree
	treeF_t free : LLFREE_TREE_FREE_BITS;
	/// Whether this tree has been reserved
	bool reserved : 1;
	/// The kind of pages this tree contains
	uint8_t kind : 2;
	/// The number of zeroed huge pages in this tree
	/// This requires kind == TREE_HUGE
	treeF_t zeroed : LLFREE_TREE_CHILDREN_ORDER + 1;
} tree_t;
_Static_assert(sizeof(tree_t) == sizeof(treeF_t), "tree size mismatch");

// The tree kinds track if a tree contains at least one page of the given kind.
// Trees can be demoted to a lower kind if memory runs out.
// They are promoted if all corresponding pages are freed.

/// Contains immovable pages
#define TREE_FIXED (uint8_t)(0u)
/// Contains movable pages
#define TREE_MOVABLE (uint8_t)(1u)
/// Contains huge pages (movability is irrelevant)
#define TREE_HUGE (uint8_t)(2u)
/// Contains at least one zeroed huge page
#define TREE_ZEROED (uint8_t)(3u)
/// Number of tree kinds
#define TREE_KINDS (uint8_t)(4u)

#define TREE_KIND_NAME(kind) \
	((char *[]){ "fixed", "movable", "huge", "zeroed" })[(kind)]

typedef struct p_range {
	treeF_t min, max;
} p_range_t;

/// Lower bound used by the tree search heuristics
#define TREE_LOWER_LIM (LLFREE_TREE_SIZE / 16)

/// Create a new tree entry
static inline ll_unused tree_t tree_new(treeF_t counter, bool reserved,
					uint8_t kind, treeF_t zeroed)
{
	assert(counter <= LLFREE_TREE_SIZE);
	assert(zeroed <= LLFREE_TREE_CHILDREN);
	assert(zeroed == 0 || kind == TREE_ZEROED);
	return (tree_t){ counter, reserved, kind, zeroed };
}

/// Increment the free counter
/// If zeroed is true and the tree is Huge, increment the zeroed counter
bool tree_inc(tree_t *self, treeF_t free, bool zeroed);

/// Decrement the free counter
/// If zeroed is true, the tree must have at least one zeroed huge page
bool tree_dec(tree_t *self, treeF_t free, bool zeroed);

/// Decrement the free counter, and downgrade the tree kind if necessary.
bool tree_dec_force(tree_t *self, treeF_t free, uint8_t kind);

/// Given require_zeroed, only update the tree if there is a zeroed or non-zeroed frame available.
bool tree_dec_zeroing(tree_t *self, treeF_t free, bool require_zeroed);

/// Try reserving the tree if the free counter is withing the range
bool tree_reserve(tree_t *self, treeF_t min, treeF_t max, uint8_t kind);

/// Adds the given counter and clears reserved
bool tree_unreserve(tree_t *self, treeF_t free, uint8_t kind, treeF_t zeroed);

// Steals the counter of a reserved tree
bool tree_steal_counter(tree_t *self, treeF_t min);

/// Increment the free counter or reserve if specified
bool tree_inc_or_reserve(tree_t *self, treeF_t free, bool *reserve,
			 treeF_t min);
