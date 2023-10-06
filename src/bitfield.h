#pragma once

#include "utils.h"

/// Amount of Bits in a Field
#define FIELDSIZE (1 << 9)
/// Number of Bytes in cacheline
#define CACHESIZE (1 << 6)
/// Bits in uint64_t -> biggest atomic size
#define ATOMICSIZE (sizeof(uint64_t) * 8)
/// number of uint64 in the Bitfield.
#define FIELD_N (FIELDSIZE / ATOMICSIZE)

/// Atomic bitfield
typedef struct bitfield {
	alignas(CACHESIZE) _Atomic(uint64_t) rows[FIELD_N];
} bitfield_t;

/// Initializes the Bitfield of 512 Bit size with all 0
///
/// Note: uses non-atomic functions because it should run only once at start and not in parallel
void field_init(bitfield_t *self);

/// Atomic search for the first unset bit and set it to 1.
result_t field_set_next(bitfield_t *field, uint64_t pfn, size_t order);

/// Atomically resets the bit at index position
result_t field_reset_bit(bitfield_t *field, size_t index);

/// Count the number of bits
int field_count_bits(bitfield_t *field);

/// Atomically checks whether the bit is set
bool field_is_free(bitfield_t *self, size_t index);

/// Helper function to Print a Bitfield on the console
void field_print(bitfield_t *field);
