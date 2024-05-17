#pragma once

#include "utils.h"

/// Index entry for every bitfield
typedef struct child {
	/// Counter for free base frames in this region
	uint16_t free : 14;
	/// Whether this has been allocated as a single huge page
	bool huge : 1;
	/// Hint for the guest that this region has been reclaimed and potentially been unmapped
	bool reclaimed : 1;
} child_t;

_Static_assert(13 > LLFREE_CHILD_ORDER, "child counter size");

/// Initializes the child entry with the given parameters
static inline child_t ll_unused child_new(uint16_t free, bool huge,
					bool reclaimed)
{
	assert(free <= LLFREE_CHILD_SIZE);
	return (child_t){ .free = free, .huge = huge, .reclaimed = reclaimed };
}

/// Increment the free counter if possible
bool child_inc(child_t *self, size_t order);

/// Decrement the free counter if possible
bool child_dec(child_t *self, size_t order, bool allow_reclaimed);

/// Alloc the child
bool child_set_huge(child_t *self, bool allow_reclaimed);
/// Free the child
bool child_clear_huge(child_t *self);

typedef struct child_pair {
	child_t first, second;
} child_pair_t;

/// Alloc a pair of child entries
bool child_set_max(child_pair_t *self, bool allow_reclaimed);
/// Free a pair of child entries
bool child_clear_max(child_pair_t *self);

/// Set the child to reclaimed if it is free and not already reclaimed
bool child_reclaim(child_t *self, bool alloc);
/// Free the child but keep it reclaimed
bool child_return(child_t *self);
/// Clear the reclaimed flag
bool child_install(child_t *self);
