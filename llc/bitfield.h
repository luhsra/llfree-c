#pragma once

#include <bits/stdint-uintn.h>
#include <stdint.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdatomic.h>

/*
 * Implements The Bitfield and behaviour.
*/
#define FIELDSIZE 512                       // Amount of Bits in a Field
#define CACHESIZE (sizeof(uint64_t) * 8)    // Bit-Size of the Biggest Datatype for atomic operations. uint64
#define N FIELDSIZE/CACHESIZE               // number of uint64 in the Bitfield.

typedef struct bitfield_512{
    alignas(CACHESIZE) _Atomic(uint64_t) rows[N];
} bitfield_512_t;

// Helping struct to store the position of a bit in a bitfield.
typedef struct pos {
    size_t row_index;     // 0 <= row_number < N
    size_t bit_index;     // 0 <= bit_number < CACHESIZE
}pos_t;

/**
 * @brief Initializes the Bitfield of 512 Bit size.
 * uses non-atomic functions because it should run only once at start and not in parallel.
 * @param number_of_free_Frames is the number of bits starting with the Value 0.
 * @param start_allocated if set, all bits will be set to 1.
 * @return The inizialized bitfield
 */
bitfield_512_t init_field(int number_of_free_Frames, bool start_allocated);

/**
* @brief Atomic search for the first unset Bit in given field and set it to 1.
* @param field Field to search in
* @return index of the set bit, on success
*         ERR_MEMORY if no free bit was found
*         ERR_RETRY  if the atomic operation failed
*/
int set_Bit(bitfield_512_t* field);

/**
* @brief Atomically resets the Bit in given field at index position
* @param field Field to search in
* @return ERR_OK on success
*         ERR_ADRESS if the bit was already reset
*/
int reset_Bit(bitfield_512_t* field,size_t index);

/**
* @brief Count the Number of bits in the Field
* works non-atomicly!
* @param field Field to count bits in.
* @return number of set Bits
*/
int count_Set_Bits(bitfield_512_t* field);

/**
* @brief Atomically checks whether the bit is set.
* @param field pointer to the field
* @param index of the Bit
* @return true if the Bit is set
*         false otherwise
*/
bool is_free_bit(bitfield_512_t* self,size_t index);

/**
* @brief Helperfunction to Print a Bitfield on the console
*/
void print_field(bitfield_512_t* field);

/**
* @brief Helperfunction to compare two bitfields
* @return true if both fields are equal or both pointer are NULL;
*         false otherwise
*/
bool equals(bitfield_512_t* field1, bitfield_512_t* field2);

/**
 * @brief finds the position of the first 0 in the bitfield
 * @param field Pointer to the bitfield
 * @param pos Pointer to a struct the position will be Stored in
 * @return  ERR_OK on success
 *          ERR_MEMORY if no unset Bit was found.
 */
int find_unset(bitfield_512_t* field,  pos_t* pos);
