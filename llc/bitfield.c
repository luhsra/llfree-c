#include "bitfield.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "enum.h"


// Helping struct to store the position of a bit in a bitfield.
typedef struct pos {
    size_t row_index;     // 0 <= row_number < N
    size_t bit_index;     // 0 <= bit_number < CACHESIZE
}pos_t;

//Translates the index of the bit to the position in the field.
static pos_t get_pos(int index){
    assert(0 <= index && index < FIELDSIZE && "max range for 512 Bits");

    pos_t pos = {index / CACHESIZE, index % CACHESIZE};
    return pos;
}

bitfield_512_t init_field(int number_of_free_Frames, bool start_allocated){
    
    assert(0 <= number_of_free_Frames && number_of_free_Frames < FIELDSIZE);
    bitfield_512_t field;

    if(start_allocated){
        for(size_t i = 0; i < N; i++){
            field.rows[i] = 0xfffffffffffffffful;
        }
        return field;
    }
    // 0 free frames mean all are free
    // is nicer with mod syntax
    if(number_of_free_Frames == 0){
        for(size_t i = 0; i < N; i++){
            field.rows[i] = 0x0ul;
        }
        return field;
    }

    // possible to have not a fully saturated bitfield
    pos_t pos = get_pos(number_of_free_Frames); //position of the last free frame
    uint64_t mask = 0xfffffffffffffffful << pos.bit_index;
    for(size_t i = 0; i < N; i++){
        if(i < pos.row_index){
            field.rows[i] = 0x0ul;
        }else if (i > pos.row_index){
            field.rows[i] = 0xfffffffffffffffful;
        }else{
            field.rows[i] = mask;
        }
    }


    return field;
}



/**
 * @brief finds the position of the first 0 in the bitfield
 * @param field Pointer to the bitfield
 * @param pos Pointer to a struct the position will be Stored in
 * @return  ERR_OK on success
 *          ERR_MEMORY if no unset Bit was found.
 *
 */
static int find_unset(bitfield_512_t* field,  pos_t* pos){
    assert(field != NULL);
    assert(pos != NULL);

    for(size_t i = 0; i < N; i++){
        uint64_t row = atomic_load(&field->rows[i]);
        // ctzll(x) -> If x is 0, the result is undefined and there are no unset bits
        if(~row == 0) continue;
        
        int ret = __builtin_ctzll(~row);   //TODO wrapper for CTZ? 

        assert(ret >= 0 && "ctzll shoud never be negative");
        assert(ret < 64 && "ctzll schould not count more zeros as there are Bits");

        pos->row_index = i;
        pos->bit_index = ret;
        return ERR_OK;
    }

    return ERR_MEMORY; // ERR_MEMORY ->Kein freies Bit in diesem Feld
}

int set_Bit(bitfield_512_t* field){
    assert(field != NULL);

    pos_t pos;
    if(find_unset(field, &pos) < 0) return ERR_MEMORY;

    uint64_t mask = 1ull << pos.bit_index; // 00...010...0 -> one at the bit-position

    uint64_t before = atomic_fetch_or(&field->rows[pos.row_index], mask);

    if((before | mask) == before){ // if no change occours the bit was already set.
        return ERR_RETRY;
    }
    return pos.row_index * CACHESIZE + pos.bit_index;
}

int reset_Bit(bitfield_512_t* field, size_t index){
    assert(field != NULL);
    assert(0 <= index && index < FIELDSIZE);

    pos_t pos = get_pos(index);

    uint64_t mask = ~(1ull << pos.bit_index); // 11...101...11 -> zero at the bit-position

    uint64_t before = atomic_fetch_and(&field->rows[pos.row_index], mask);

    if((before & mask) == before){ // bit were already reset
        return ERR_ADDRESS;
    }

    return ERR_OK;
}

int count_Set_Bits(bitfield_512_t* field){
    assert(field != NULL);

    int counter = 0;
    for(size_t i = 0; i < N; i++){
        counter += __builtin_popcountll(field->rows[i]); //TODO wrapper popcount ?
    }
    
    assert(0 <= counter && counter <= FIELDSIZE);
    return counter;
}


bool is_free_bit(bitfield_512_t* self,size_t index){
    assert(self != NULL);
    assert(0 <= index && index < FIELDSIZE);
    pos_t pos = get_pos(index);

    uint64_t row = atomic_load(&self->rows[pos.row_index]);
    uint64_t mask = 1ul << pos.bit_index;

    return (row & mask) == 0;
}


void print_field(bitfield_512_t* field){
    assert(field != NULL);

    printf("Field in HEX: MSB to LSB\n");
    for(size_t i = 0; i < N; i++){
        uint16_t* s = (uint16_t*) &(field->rows[i]);
        printf("%04X %04X %04X %04X\n", s[3], s[2], s[1], s[0]);
    }
}



// true if both fields are equal or both pointer are NULL;
bool equals(bitfield_512_t* f1, bitfield_512_t* f2){
    if(f1 == NULL && f2 == NULL) return true;
    if(f1 == NULL || f2 == NULL) return false;

    for(size_t i = 0; i < N; i++){
        if(f1->rows[i] != f2->rows[i]) return false;
    }
    return true;
}

