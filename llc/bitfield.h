#pragma once

#include <bits/stdint-uintn.h>
#include <stdint.h>
#include <stdalign.h>
#include <stdbool.h>
#include "utils.h"
/*
 * Implements The Bitfield and behaviour.
*/
#define FIELDSIZE (1 << 9)                    // Amount of Bits in a Field
#define CACHESIZE (sizeof(uint64_t) * 8)    // Bit-Size of the Biggest Datatype for atomic operations. uint64
#define N FIELDSIZE/CACHESIZE               // number of uint64 in the Bitfield.

typedef struct bitfield{
    alignas(CACHESIZE) _Atomic(uint64_t) rows[N];
} bitfield_t;

// Helping struct to store the position of a bit in a bitfield.
typedef struct pos {
    size_t row_index;     // 0 <= row_number < N
    size_t bit_index;     // 0 <= bit_number < sizeof(uint64_t)
}pos_t;

/**
 * @brief Initializes the Bitfield of 512 Bit size.
 * uses non-atomic functions because it should run only once at start and not in parallel.
 * @param number_of_free_Frames is the number of bits starting with the Value 0.
 * @param start_allocated if set, all bits will be set to 1.
 * @return The inizialized bitfield
 */
bitfield_t field_init(int number_of_free_Frames, bool all_free);

/**
* @brief Atomic search for the first unset Bit in given field and set it to 1.
* @param field Field to search in
* @return index of the set bit, on success
*         ERR_MEMORY if no free bit was found
*         ERR_RETRY  if the atomic operation failed
*/
int64_t field_set_Bit(bitfield_t* field);

/**
* @brief Atomically resets the Bit in given field at index position
* @param field Field to search in
* @return ERR_OK on success
*         ERR_ADRESS if the bit was already reset
*/
int field_reset_Bit(bitfield_t* field,size_t index);

/**
* @brief Count the Number of bits in the Field
* @param field Field to count bits in.
* @return number of set Bits
*/
int field_count_Set_Bits(bitfield_t* field);

/**
* @brief Atomically checks whether the bit is set.
* @param field pointer to the field
* @param index of the Bit
* @return true if the Bit is set
*         false otherwise
*/
bool field_is_free(bitfield_t* self,size_t index);

/**
* @brief Helperfunction to Print a Bitfield on the console
*/
void field_print(bitfield_t* field);

/**
* @brief Helperfunction to compare two bitfields
* @return true if both fields are equal or both pointer are NULL;
*         false otherwise
*/
bool field_equals(bitfield_t* field1, bitfield_t* field2);

/**
 * @brief finds the position of the first 0 in the bitfield
 * @param field Pointer to the bitfield
 * @param pos Pointer to a struct the position will be Stored in
 * @return  ERR_OK on success
 *          ERR_MEMORY if no unset Bit was found.
 */
int field_find_unset(bitfield_t* field,  pos_t* pos);
