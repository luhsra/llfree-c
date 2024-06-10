#pragma once

#include "utils.h"

#if LLFREE_TREE_ORDER < 16
/// Free Counter
typedef uint16_t treeF_t;
#elif LLFREE_TREE_ORDER < 32
/// Free Counter
typedef uint32_t treeF_t;
#else
#error Counter overflow
#endif

/// Tree entry
typedef struct tree {
	/// Number of free frames in this tree
	treeF_t free : LLFREE_TREE_ORDER + 1;
	/// Whether this tree has been reserved
	bool reserved : 1;
	/// The kind of pages this tree contains
	uint8_t kind : 2;
} tree_t;

/// Contains immovable pages
#define TREE_FIXED (uint8_t)(0u)
/// Contains movable pages
#define TREE_MOVABLE (uint8_t)(1u)
/// Contains huge pages (movability is irrelevant)
#define TREE_HUGE (uint8_t)(2u)
#define TREE_KINDS (uint8_t)(3u)

typedef struct p_range {
	treeF_t min, max;
} p_range_t;

/// Lower bound used by the tree search heuristics
#define TREE_LOWER_LIM (LLFREE_TREE_SIZE / 16)

/// Create a new tree entry
static inline ll_unused tree_t tree_new(treeF_t counter, bool reserved,
				      uint8_t kind)
{
	assert(counter <= LLFREE_TREE_SIZE); // max limit for 15 bit
	return (tree_t){ counter, reserved, kind };
}

/// Increment the free counter
bool tree_inc(tree_t *self, treeF_t free);

/// Decrement the free counter
bool tree_dec(tree_t *self, treeF_t free);

bool tree_dec_force(tree_t *self, treeF_t free, uint8_t kind);

/// Try reserving the tree if the free counter is withing the range
bool tree_reserve(tree_t *self, treeF_t min, treeF_t max, uint8_t kind);

/// Adds the given counter and clears reserved
bool tree_unreserve(tree_t *self, treeF_t free, uint8_t kind);

// Steals the counter of a reserved tree
bool tree_steal_counter(tree_t *self, treeF_t min);

/// Increment the free counter or reserve if specified
bool tree_inc_or_reserve(tree_t *self, treeF_t free, bool *reserve,
			 treeF_t min);
