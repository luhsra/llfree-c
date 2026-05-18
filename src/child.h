#pragma once

#include "utils.h"

#define CHILD_FREE_BITS ((sizeof(uint16_t) * 8) - 1)
_Static_assert(CHILD_FREE_BITS > LLFREE_CHILD_ORDER, "child counter size");

/// Index entry for every bitfield
typedef struct child {
	/// Counter for free base frames in this region
	uint16_t free : CHILD_FREE_BITS;
	/// Whether this has been allocated as a single huge page
	bool huge : 1;
} child_t;
_Static_assert(sizeof(child_t) == sizeof(uint16_t), "child size mismatch");

/// Initializes the child entry with the given parameters
static inline child_t ll_unused child_new(uint16_t free, bool huge)
{
	assert(free <= LLFREE_CHILD_SIZE);
	assert(!huge || free == 0);
	return (child_t){ .free = free, .huge = huge };
}

/// Increment the free counter if possible
bool child_inc(child_t *self, size_t order);

/// Decrement the free counter if possible
bool child_dec(child_t *self, size_t order);
