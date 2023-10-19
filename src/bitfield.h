#pragma once

#include "utils.h"

#define FIELD_N (CHILD_SIZE / ATOMIC_SIZE)

/// Atomic bitfield
typedef struct bitfield {
	alignas(CACHE_SIZE) _Atomic(uint64_t) rows[FIELD_N];
} bitfield_t;

/// Initializes the Bitfield of 512 Bit size with all 0
///
/// Note: uses non-atomic functions because it should run only once at start and not in parallel
void field_init(bitfield_t *self);

/// Atomic search for the first unset bit and set it to 1.
result_t field_set_next(bitfield_t *field, uint64_t pfn, size_t order);

/// Atomically resets the bit at index position
result_t field_toggle(bitfield_t *field, size_t index, size_t order, bool expected);

/// Count the number of bits
int field_count_bits(bitfield_t *field);

/// Atomically checks whether the bit is set
bool field_is_free(bitfield_t *self, size_t index);

/// Helper function to Print a Bitfield on the console
void field_print(bitfield_t *field);
