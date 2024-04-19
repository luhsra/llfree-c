#pragma once

#include "utils.h"

/// Is this child mapped
#define LL_CS_MAP ((uint8_t)0)
/// If this child is not mapped (inflated)
#define LL_CS_INF ((uint8_t)1)
/// If this child is in the process of being mapped (deflating)
#define LL_CS_DEF ((uint8_t)2)

/// Index entry for every bitfield
typedef struct child {
	uint16_t free : 13;
	bool huge : 1;
	uint8_t state : 2;
} child_t;

_Static_assert(13 > LLFREE_CHILD_ORDER, "child counter size");

/// Initializes the child entry with the given parameters
static inline child_t _unused child_new(uint16_t free, bool huge, uint8_t state)
{
	assert(free <= LLFREE_CHILD_SIZE);
	return (child_t){ .free = free, .huge = huge, .state = state };
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
