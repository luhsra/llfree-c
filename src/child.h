#pragma once

#include "utils.h"

// child has 512 entry's
#define CHILDSIZE FIELDSIZE

/// Index entry for every bitfield
typedef struct child {
	uint16_t counter : 15;
	bool huge : 1;
} child_t;

/// Initializes the child entry with the given parameters
static inline child_t _unused child_new(uint16_t counter, bool flag)
{
	return (child_t){ .counter = counter, .huge = flag };
}

/// Increment the free counter if possible
bool child_counter_inc(child_t *self, _void v);

/// Decrement the free counter if possible
bool child_counter_dec(child_t *self, _void v);

/// Free the entry as huge page if possible
bool child_reserve_huge(child_t *self, _void v);


typedef struct child_pair {
        child_t first, second;
} child_pair_t;

bool child_reserve_max(child_pair_t *self, _void v);
