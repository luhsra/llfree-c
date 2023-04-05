#include "bitfield.h"
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

static pos_t get_pos(int index){
    assert(0 <= index && index < FIELDSIZE && "max range for 512 Bits");

    pos_t pos = {index / CACHESIZE, index % CACHESIZE};
    return pos;
}

//initializes the bitfield with zeros
bitfield_512_t init_field(int number_of_free_Frames, bool start_allocated){
    assert(0 <= number_of_free_Frames && number_of_free_Frames <= FIELDSIZE);
    bitfield_512_t field;

    if(start_allocated){
        for(int i = 0; i < N; i++){
            field.rows[i] = 0xffffffffffffffffull;
        }
        return field;
    }
    // 0 free frames mean all are free
    // is nicer with mod syntax
    if(number_of_free_Frames == 0){
        for(int i = 0; i < N; i++){
            field.rows[i] = 0x0;
        }
        return field;
    }

    // possible to have not a fully saturated bitfield
    pos_t pos = get_pos(number_of_free_Frames);
    uint64_t mask = 0xfffffffffffffffful << pos.bit_number;
    for(int i = 0; i < N; i++){
        if(i < pos.row_number){
            field.rows[i] = 0x0ull;
        }else if (i > pos.row_number){
            field.rows[i] = 0xffffffffffffffffull;
        }else{
            field.rows[i] = mask;
        }
    }


    return field;
}



//Find first unset Bit
int find_unset(bitfield_512_t* field,  size_t* index){
    assert(field != NULL);

    for(int i = 0; i < N; i++){
        int ret;
        // ctzll(x) -> If x is 0, the result is undefined. 
        if(~field->rows[i] == 0) ret = CACHESIZE;
        else ret = __builtin_ctzll(~field->rows[i]);        

        assert(ret >= 0 && "ctzll shoud never be negative");
        assert(ret <= 64 && "ctzll schould not count more zeros as there are Bits");

        if(ret < CACHESIZE){
            *index = i * CACHESIZE + ret;
            return 0;
        }
    }

    return -1; // ERR_MEMORY ->Kein freies Bit in diesem Feld
}

//atomicly set Bin in position n
int set_Bit(bitfield_512_t* field, size_t index){
    assert(field != NULL);
    assert(0 <= index && index < FIELDSIZE);

    pos_t pos = get_pos(index);

    uint64_t mask = 1ull << pos.bit_number; // 00...010...0 -> one at the bit-position

    uint64_t before = __c11_atomic_fetch_or(&field->rows[pos.row_number], mask, __ATOMIC_ACQ_REL);

    if((before & mask) != 0ull){ // bit was already set
        return -1;
    }
    return 0;
}

//atomicly unsets Bit in position n
int reset_Bit(bitfield_512_t* field, size_t index){
    assert(field != NULL);
    assert(0 <= index && index < FIELDSIZE);

    pos_t pos = get_pos(index);

    uint64_t mask = ~(1ull << pos.bit_number); // 11...101...11 -> zero at the bit-position

    uint64_t before = __c11_atomic_fetch_and(&field->rows[pos.row_number], mask, __ATOMIC_ACQ_REL);

    if((before & mask) == before){ // bit were already reset
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