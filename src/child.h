#pragma once

#include "utils.h"

/// Index entry for every bitfield
typedef struct child {
	uint16_t free : 14;
	bool huge : 1;
	bool inflated : 1;
} child_t;

_Static_assert(13 > LLFREE_CHILD_ORDER, "child counter size");

/// Initializes the child entry with the given parameters
static inline child_t _unused child_new(uint16_t free, bool huge, bool inflated)
{
	assert(free <= LLFREE_CHILD_SIZE);
	return (child_t){ .free = free, .huge = huge, .inflated = inflated };
}

/// Increment the free counter if possible
bool child_inc(child_t *self, size_t order);

/// Decrement the free counter if possible
bool child_dec(child_t *self, size_t order);

/// Free the entry as huge page if possible
bool child_reserve_huge(child_t *self);

typedef struct child_pair {
	child_t first, second;
} child_pair_t;

/// Reserve a pair of child entries
bool child_reserve_max(child_pair_t *self);

/// Set the child to inflated if it is free and mapped
bool child_inflate(child_t *self);

/// Set the child to mapped if it is inflated or deflating
bool child_deflate(child_t *self);
