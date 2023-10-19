#pragma once

#include "utils.h"

// child has 512 entry's
#define CHILDSIZE CHILD_SIZE

/// Index entry for every bitfield
typedef struct child {
	uint16_t free : 15;
	bool huge : 1;
} child_t;

/// Initializes the child entry with the given parameters
static inline child_t _unused child_new(uint16_t free, bool flag)
{
	return (child_t){ .free = free, .huge = flag };
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
