#pragma once

#include "utils.h"

/// Index entry for every bitfield
typedef struct child {
	uint16_t free : 14;
	bool huge : 1;
	bool reported : 1;
} child_t;

/// Initializes the child entry with the given parameters
static inline child_t _unused child_new(uint16_t free, bool huge, bool reported)
{
	assert(free <= LLFREE_CHILD_SIZE);
	return (child_t){ .free = free, .huge = huge, .reported = reported };
}

/// Increment the free counter if possible
bool child_inc(child_t *self, size_t order);

/// Decrement the free counter if possible
bool child_dec(child_t *self, size_t order);

/// Free the entry as huge page if possible
bool child_reserve_huge(child_t *self, llflags_t flags);

typedef struct child_pair {
	child_t first, second;
} child_pair_t;

/// Reserve a pair of child entries
bool child_reserve_max(child_pair_t *self, llflags_t flags);
