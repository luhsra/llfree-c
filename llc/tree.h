#include "utils.h"

typedef struct tree {
	uint16_t counter : 15;
	bool flag : 1;
} tree_t;

typedef struct range {
	uint16_t min, max;
} range_t;

static const size_t TREESIZE = (1 << 9 << 5);
static const size_t TREE_LOWER_LIM = 2 << HP_ORDER;
static const size_t TREE_UPPER_LIM = TREESIZE - (8 << HP_ORDER);

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
bool tree_counter_inc(tree_t *self, size_t order);

/// Decrement the free counter if possible
bool tree_counter_dec(tree_t *self, size_t order);

/// Try reserving the tree if the free counter is withing the range
bool tree_reserve(tree_t *self, range_t free);

/// Adds the given counter to the existing counter and sets the flag to 0
bool tree_writeback(tree_t *self, uint16_t free_counter);

// Steals the counter of a reserved tree
bool tree_steal_counter(tree_t *self, _void v);
