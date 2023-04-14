#pragma once

#include <stdint.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdatomic.h>

//TODO Nice Function Descriptions


/*
Implements The Bitfield and behaviour.
*/
#define FIELDSIZE 512           // Amount of Bits in a Field
#define CACHESIZE 64            // Size of the Biggest Datatype for atomic operations. uint64
#define N FIELDSIZE/CACHESIZE   // number of uint64 in the Bitfield.

typedef struct bitfield_512{
    alignas(CACHESIZE) _Atomic(uint64_t) rows[N];
} bitfield_512_t;

// position of a bit in a bitfield
typedef struct pos {
    int row_number;     // 0 <= row_number < N
    int bit_number;     // 0 <= bit_number < CACHESIZE
}pos_t;


//initializes the bitfield with zeros
bitfield_512_t init_field(int number_of_free_Frames, bool start_allocated);

//finds unset bit and sets it. returns the index on success, ERR_MEM on failure
int set_Bit(bitfield_512_t* field);

//atomicly unsets Bit in position pos
int reset_Bit(bitfield_512_t* field,size_t index);

//returns the number of Bits set
int count_Set_Bits(bitfield_512_t* field);

//checks it the bit is free
bool is_free_bit(bitfield_512_t* self,size_t index);

//prints the bitfield
void print_field(bitfield_512_t* field);

// true if both fields are equal or both pointer are NULL;
bool equals(bitfield_512_t* field1, bitfield_512_t* field2);
