#include "bitfield.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "enum.h"
#include "utils.h"

// Helping struct to store the position of a bit in a bitfield.
typedef struct pos {
    size_t row_index;     // 0 <= row_number < N
    size_t bit_index;     // 0 <= bit_number < sizeof(uint64_t)
}pos_t;

//Translates the index of the bit to the position in the field.
static pos_t get_pos(uint64_t index){
    index = index & (FIELDSIZE -1);

    pos_t pos = {index / ATOMICSIZE, index % ATOMICSIZE};
    return pos;
}


void field_init(bitfield_t* self){
    assert(self != NULL);
    for(uint64_t i = 0; i < N; ++i){
        self->rows[i] = 0;
    }
}



/**
 * @brief finds the position of the first 0 in the bitfield
 * @param field Pointer to the bitfield
 * @param pos Pointer to a struct the position will be Stored in
 * @return  ERR_OK on success
 *          ERR_MEMORY if no unset Bit was found.
 */
static int find_unset(uint64_t val){

    // ctzll(x) -> If x is 0, the result is undefined and there are no unset bits
    if(~val == 0) return ERR_MEMORY;

    int ret = __builtin_ctzll(~val);

    assert(ret >= 0 && "ctzll shoud never be negative");
    assert(ret < 64 && "ctzll schould not count more zeros as there are Bits");

    return ret;
}

int64_t field_set_Bit(bitfield_t* field, const uint64_t pfn){
    assert(field != NULL);

    uint64_t row = atomic_from_pfn(pfn) % N;

    do {
        const size_t _offset = (row) % (((1 << 9) / (sizeof(uint64_t) * 8)));
        const size_t _base_idx = (row)-_offset;
        for (size_t _i = 0; _i < (((1 << 9) / (sizeof(uint64_t) * 8))); ++_i) {
            const size_t current_i =
                _base_idx +
                ((_i + _offset) % (((1 << 9) / (sizeof(uint64_t) * 8))));
            {
              int pos;
              while ((pos = find_unset(__c11_atomic_load(
                          &field->rows[current_i], memory_order_acquire))) >=
                     0) {
                uint64_t mask = 1ul << pos;
                uint64_t before =
                    __c11_atomic_fetch_or(&field->rows[current_i], mask, 5);
                if ((before | mask) != before) {
                  return current_i * (sizeof(uint64_t) * 8) + pos;
                }
              };
            }
        }
    } while (0);
    return ERR_MEMORY;
}

int field_reset_Bit(bitfield_t* field, size_t index){
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



int field_count_Set_Bits(bitfield_t* field){
    assert(field != NULL);
    int counter = 0;
    for(size_t i = 0; i < N; i++){
        uint64_t row = load(&field->rows[i]);
        counter += __builtin_popcountll(row); //TODO wrapper popcount ?
    }

    assert(0 <= counter && counter <= FIELDSIZE);
    return counter;
}


bool field_is_free(bitfield_t* self,size_t index){
    assert(self != NULL);
    assert(0 <= index && index < FIELDSIZE);
    pos_t pos = get_pos(index);

    uint64_t row = load(&self->rows[pos.row_index]);
    uint64_t mask = 1ul << pos.bit_index;

    return (row & mask) == 0;
}


void field_print(bitfield_t* field){
    assert(field != NULL);

    printf("Field in HEX: MSB to LSB\n");
    for(size_t i = 0; i < N; i++){
        uint16_t* s = (uint16_t*) &(field->rows[i]);
        printf("%04X %04X %04X %04X\n", s[3], s[2], s[1], s[0]);
    }
}



// true if both fields are equal or both pointer are NULL;
bool field_equals(bitfield_t* f1, bitfield_t* f2){
    if(f1 == NULL && f2 == NULL) return true;
    if(f1 == NULL || f2 == NULL) return false;

    for(size_t i = 0; i < N; i++){
        if(f1->rows[i] != f2->rows[i]) return false;
    }
    return true;
}

