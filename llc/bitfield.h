#pragma once

#include "utils.h"
/*
 * Implements The Bitfield and behaviour.
*/
#define FIELDSIZE (1 << 9) // Amount of Bits in a Field
#define CACHESIZE (1 << 6) // Number of Bytes in cacheline
#define ATOMICSIZE \
	(sizeof(uint64_t) * 8) // Bits in uint64_t -> biggest atomic size
#define FIELD_N (FIELDSIZE / ATOMICSIZE) // number of uint64 in the Bitfield.

typedef struct bitfield {
	alignas(CACHESIZE) _Atomic(uint64_t) rows[FIELD_N];
} bitfield_t;

/**
 * @brief Initializes the Bitfield of 512 Bit size with all 0.
 * uses non-atomic functions because it should run only once at start and not in parallel.
 * @param self pointer to field
 */
void field_init(bitfield_t *self);

/**
* @brief Atomic search for the first unset Bit in given field and set it to 1.
* @param field Field to search in
* @return index of the set bit, on success
*         ERR_MEMORY if no free bit was found
*         ERR_RETRY  if the atomic operation failed
*/
result_t field_set_Bit(bitfield_t *field, const uint64_t pfn);

/**
* @brief Atomically resets the Bit in given field at index position
* @param field Field to search in
* @return ERR_OK on success
*         ERR_ADDRESS if the bit was already reset
*/
result_t field_reset_bit(bitfield_t *field, size_t index);

/**
* @brief Count the Number of bits in the Field
* @param field Field to count bits in.
* @return number of set Bits
*/
int field_count_bits(bitfield_t *field);

/**
* @brief Atomically checks whether the bit is set.
* @param field pointer to the field
* @param index of the Bit
* @return true if the Bit is set
*         false otherwise
*/
bool field_is_free(bitfield_t *self, size_t index);

/**
* @brief Helper function to Print a Bitfield on the console
*/
void field_print(bitfield_t *field);

/**
* @brief Helper function to compare two bitfields
* @return true if both fields are equal or both pointer are NULL;
*         false otherwise
*/
bool field_equals(bitfield_t *field1, bitfield_t *field2);
