#pragma once

#include "utils.h"

/// Tree entry
typedef struct tree {
	/// Number of free frames in this tree
	uint16_t free : 15;
	/// Whether this tree has been reserved
	bool reserved : 1;
} tree_t;

typedef struct p_range {
	uint16_t min, max;
} p_range_t;

/// Lower bound used by the tree search heuristics
#define TREE_LOWER_LIM (LLFREE_TREE_SIZE / 16)
/// Upper bound used by the tree search heuristics
#define TREE_UPPER_LIM (LLFREE_TREE_SIZE - (LLFREE_TREE_SIZE / 32))
static const p_range_t TREE_PARTIAL = { TREE_LOWER_LIM, TREE_UPPER_LIM };
static const p_range_t TREE_NOT_FULL = { TREE_LOWER_LIM, LLFREE_TREE_SIZE };
static const p_range_t TREE_NOT_FREE = { 0, TREE_UPPER_LIM };
static const p_range_t TREE_ANY = { 0, LLFREE_TREE_SIZE };

/// Create a new tree entry
static inline _unused tree_t tree_new(uint16_t counter, bool flag)
{
	assert(counter <= LLFREE_TREE_SIZE); // max limit for 15 bit
	return (tree_t){ counter, flag };
}

/// Increment the free counter
bool tree_inc(tree_t *self, size_t free);

/// Try reserving the tree if the free counter is withing the range
bool tree_reserve(tree_t *self, size_t min, size_t max);

/// Adds the given counter and clears reserved
bool tree_writeback(tree_t *self, size_t free);

// Steals the counter of a reserved tree
bool tree_steal_counter(tree_t *self, size_t min);

/// Increment the free counter or reserve if specified
bool tree_inc_or_reserve(tree_t *self, size_t free, bool *reserve, size_t min,
			 size_t max);
