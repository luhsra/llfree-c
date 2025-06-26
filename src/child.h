#pragma once

#include "utils.h"

#define CHILD_FREE_BITS ((sizeof(uint16_t) * 8) - 3)
_Static_assert(CHILD_FREE_BITS > LLFREE_CHILD_ORDER, "child counter size");

/// Index entry for every bitfield
typedef struct child {
	/// Counter for free base frames in this region
	uint16_t free : CHILD_FREE_BITS;
	/// Whether this has been allocated as a single huge page
	bool huge : 1;
	/// Hint for the guest that this region has been reclaimed and potentially been unmapped
	bool reclaimed : 1;
	/// Whether this huge page is already zeroed
	bool zeroed : 1;
} child_t;
_Static_assert(sizeof(child_t) == sizeof(uint16_t), "child size mismatch");

/// Initializes the child entry with the given parameters
static inline child_t ll_unused child_new(uint16_t free, bool huge,
					  bool reclaimed, bool zeroed)
{
	assert(free <= LLFREE_CHILD_SIZE);
	assert(!huge || (free == 0 && !zeroed));
	assert(!zeroed || free == LLFREE_CHILD_SIZE);
	return (child_t){ .free = free,
			  .huge = huge,
			  .reclaimed = reclaimed,
			  .zeroed = zeroed };
}

/// Increment the free counter if possible
bool child_inc(child_t *self, size_t order);

/// Decrement the free counter if possible
bool child_dec(child_t *self, size_t order, bool allow_reclaimed);

/// Alloc the child
bool child_set_huge(child_t *self, bool allow_reclaimed, bool zeroed);
/// Free the child
bool child_clear_huge(child_t *self, bool zeroed);

typedef struct child_pair {
	child_t first, second;
} child_pair_t;

/// Alloc a pair of child entries
bool child_set_max(child_pair_t *self, bool allow_reclaimed, bool zeroed);
/// Free a pair of child entries
bool child_clear_max(child_pair_t *self, bool zeroed);

/// Set the child to reclaimed if it is free and not already reclaimed
bool child_reclaim(child_t *self, bool alloc, bool not_reclaimed, bool not_zeroed);
/// Free the child but keep it reclaimed
bool child_return(child_t *self, bool install);
/// Clear the reclaimed flag
bool child_install(child_t *self);
