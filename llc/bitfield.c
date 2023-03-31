#include "bitfield.h"
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

static pos_t get_pos(int index){
    assert(0 <= index && index < 512 && "max range for 512 Bits");

    pos_t pos = {index % CACHESIZE, index / CACHESIZE};
    return pos;
}

//initializes the bitfield with zeros
int init_field(bitfield_512_t* field, int number_Of_free_Frames){
    assert(field != NULL);
    assert(0 <= number_Of_free_Frames && number_Of_free_Frames <= FIELDSIZE);


    if(number_Of_free_Frames < FIELDSIZE){
        // possible to ave not a fully saturated bitfield
        pos_t pos = get_pos(number_Of_free_Frames);
        (void) pos;
        // TODO
    }


    for(int i = 0; i < N; i++){
        field->rows[i] = 0ull;
    }
    return 0;
}



//Find first unset Bit
int find_unset(bitfield_512_t* field, pos_t* pos){
    assert(field != NULL);

    for(int i = 0; i < N; i++){
        int ret;
        // ctzll(x) -> If x is 0, the result is undefined. 
        if(~field->rows[i] == 0) ret = CACHESIZE;
        else ret = __builtin_ctzll(~field->rows[i]);        

        assert(ret >= 0 && "ctzll shoud never be negative");
        assert(ret <= 64 && "ctzll schould not count more zeros as there are Bits");

        if(ret < CACHESIZE){
            pos->bit_number = ret;
            pos->row_number = i;
            return 0;
        }
    }

    return -1; // ERR_MEMORY ->Kein freies Bit in diesem Feld
}

//atomicly set Bin in position n
int set_Bit(bitfield_512_t* field, pos_t pos){
    assert(field != NULL);
    assert(pos.bit_number < CACHESIZE);
    assert(pos.row_number < N);

    uint64_t mask = 1ull << pos.bit_number; // 00...010...0 -> one at the bit-position

    
    field->rows[pos.row_number] |= mask;

    uint64_t before = __c11_atomic_fetch_or(&field->rows[pos.row_number], mask, __ATOMIC_ACQ_REL);

    if((before & mask) != 0ull){ // bit was already set
        return -1;
    }
    return 0;
}

//atomicly unsets Bit in position n
int reset_Bit(bitfield_512_t* field, pos_t pos){
    assert(field != NULL);
    assert(pos.bit_number < CACHESIZE);
    assert(pos.row_number < N);

    uint64_t mask = ~(1ull << pos.bit_number); // 11...101...11 -> zero at the bit-position

    uint64_t before = __c11_atomic_fetch_and(&field->rows[pos.row_number], mask, __ATOMIC_ACQ_REL);

    if((before | mask) != before){ // bit were already reset
        return -1;
    }

    return 0;
}

//returns the number os set Bits
int count_Set_Bits(bitfield_512_t* field){
    assert(field != NULL);

    int counter = 0;
    for(int i = 0; i < N; i++){
        counter += __builtin_popcountll(field->rows[i]);
    }
    
    assert(0 <= counter && counter <= FIELDSIZE);
    return counter;
}


void print_field(bitfield_512_t* field){
    assert(field != NULL);

    printf("Field in HEX: MSB to LSB\n");
    for(int i = 0; i < N; i++){
        uint16_t* s = (uint16_t*) &(field->rows[i]);
        printf("%04X %04X %04X %04X\n", s[3], s[2], s[1], s[0]);
    }
}



// true if both fields are equal or both pointer are NULL;
bool equals(bitfield_512_t* f1, bitfield_512_t* f2){
    if(f1 == NULL && f2 == NULL) return true;
    if(f1 == NULL || f2 == NULL) return false;

    for(int i = 0; i < N; i++){
        if(f1->rows[i] != f2->rows[i]) return false;
    }
    return true;
}