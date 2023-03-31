#include <stdint.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdatomic.h>

/*
Implements The Bitfield and bahaviour.
*/
#define FIELDSIZE 512           // Amount of Bits in a Field
#define CACHESIZE 64            // Size of the Biggest Datatype for atomic operations. uint64
#define N FIELDSIZE/CACHESIZE   // number of uint64 in the Bitfield.

typedef struct bitfield_512{
    alignas(CACHESIZE) _Atomic(uint64_t) rows[N];
} bitfield_512_t;

// position of a bit in a bitfield
typedef struct pos {
    int bit_number;     // 0 <= bit_number < CACHESIZE
    int row_number;     // 0 <= row_number < N
}pos_t;


//initializes the bitfield with zeros
int init_field(bitfield_512_t* field, int number_Of_free_Frames);

//Find first unset Bit. Stores position in pos
int find_unset(bitfield_512_t* field, pos_t* pos);

//atomicly set Bin in position pos
int set_Bit(bitfield_512_t* field, pos_t pos);

//atomicly unsets Bit in position pos
int reset_Bit(bitfield_512_t* field, pos_t pos);

//returns the number of Bits set
int count_Set_Bits(bitfield_512_t* field);

//prints the bitfield
void print_field(bitfield_512_t* field);

// true if both fields are equal or both pointer are NULL;
bool equals(bitfield_512_t* field1, bitfield_512_t* field2);
