#pragma once

#include "llfree_platform.h"
#include "utils.h"

#define FIELD_N (LLFREE_CHILD_SIZE / LLFREE_ATOMIC_SIZE)

/// Atomic bitfield
typedef struct bitfield {
	_Atomic(uint64_t) rows[FIELD_N]
		ll_align(LLFREE_CACHE_SIZE);
} bitfield_t;

/// Initializes the Bitfield of 512 Bit size with all 0
///
/// Note: uses non-atomic functions because it should run only once at start and not in parallel
void field_init(bitfield_t *self);

/// Atomic search for the first unset bit and set it to 1.
llfree_result_t field_set_next(bitfield_t *field, uint64_t start_frame,
			       size_t order);

/// Atomically resets the bit at index position
llfree_result_t field_toggle(bitfield_t *field, size_t index, size_t order,
			     bool expected);

/// Count the number of bits
size_t field_count_ones(bitfield_t *field);

/// Atomically checks whether the bit is set
bool field_is_free(bitfield_t *self, size_t index);

#ifdef STD
/// Helper function to Print a Bitfield on the console
void field_print(bitfield_t *field);
#endif
