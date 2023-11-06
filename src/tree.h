#pragma once

#include "utils.h"

/// Tree entry
typedef struct tree {
	/// Number of free frames in this tree
	uint16_t free : 15;
	/// Whether this tree has been reserved
	bool reserved : 1;
} tree_t;

typedef struct range {
	uint16_t min, max;
} range_t;

/// Number of frames a tree manages
#define TREESIZE ((1 << HP_ORDER) << 5)

/// Lower bound used by the tree search heuristics
#define TREE_LOWER_LIM (2 << HP_ORDER)
/// Upper bound used by the tree search heuristics
#define TREE_UPPER_LIM (TREESIZE - (8 << HP_ORDER))
static const range_t TREE_PARTIAL = { TREE_LOWER_LIM, TREE_UPPER_LIM };
static const range_t TREE_FREE = { TREE_UPPER_LIM, TREESIZE };
static const range_t TREE_FULL = { 0, TREE_LOWER_LIM };

/// Create a new tree entry
static inline _unused tree_t tree_new(uint16_t counter, bool flag)
{
	assert(counter <= TREESIZE); // max limit for 15 bit
	return (tree_t){ counter, flag };
}

/// Increment the free counter if possible
bool tree_inc(tree_t *self, size_t free);

/// Try reserving the tree if the free counter is withing the range
bool tree_reserve(tree_t *self, size_t min, size_t max);

/// Adds the given counter and clears reserved
bool tree_writeback(tree_t *self, uint16_t free);

// Steals the counter of a reserved tree
bool tree_steal_counter(tree_t *self, size_t min);
