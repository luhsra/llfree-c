#include "lower.h"
#include "bitfield.h"
#include "flag_counter.h"
#include "pfn.h"
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include "enum.h"

#include <stdio.h>


static size_t div_ceil(uint64_t a, int b){
    //wenn es einen Rest gibt muss aufgerundet werden
    return a % b ? a / b + 1 : a / b;
}

static size_t num_of_childs(lower_t* self){
    return div_ceil(self->length, FIELDSIZE);
}


// allocs memory for initialisation
lower_t* init_default(pfn_t start_pfn, uint64_t len){
    lower_t* self = malloc(sizeof(lower_t));
    assert(self != NULL);

    self->start_pfn = start_pfn;
    self->length = len;

    size_t childnumber = num_of_childs(self);
    self->fields = malloc(sizeof(bitfield_512_t) * childnumber);
    assert(self->fields != NULL);
    self->childs = malloc(sizeof(flag_counter_t) * childnumber);
    assert(self->childs != NULL);

    return self;
}

int init_lower(lower_t* self, pfn_t start_pfn, uint64_t len, bool free_all){
    assert(self != NULL);

    self->start_pfn = start_pfn;
    self->length = len;
    size_t childnumber = num_of_childs(self);

    for(size_t i = 0; i < childnumber -1; i++){
        self->fields[i] = init_field(0, free_all);
        self->childs[i]= init_flag_counter(free_all ? 0: FIELDSIZE, false);
    }
    size_t frames_in_last_field = self->length % FIELDSIZE;
    self->fields[childnumber -1] = init_field(frames_in_last_field, free_all);
    
    if(frames_in_last_field == 0) frames_in_last_field = FIELDSIZE;
    self->childs[childnumber -1] = init_flag_counter(free_all ? 0 :frames_in_last_field, false);
    return ERR_OK;
}

static size_t get_child_index(lower_t* self, pfn_t pfn){
    return (pfn - self->start_pfn) / FIELDSIZE;
}

int get(lower_t* self, size_t start, size_t order, pfn_t* ret){
    (void) order; //TODO Different Orders
    size_t index_start = get_child_index(self, start);
    index_start /= 32; //allways search in a chunk of 32 childs
    size_t index_end = index_start + 32;
    if(index_end > num_of_childs(self)) index_end = num_of_childs(self);

    for(size_t index = index_start;index < index_end; index++){
        uint16_t child_counter = get_counter(&self->childs[index]);
        if(child_counter <= 0) continue;

        int result = child_counter_dec(&self->childs[index]);
        assert(result == ERR_OK); //TODO Return Error
        int pos;
        while (true) { //the atomic counter assures that here must be a free bit left in the field
            pos = set_Bit(&self->fields[index]);
            if(pos >= 0) break;
        }
        *ret = index * FIELDSIZE + pos;
        return ERR_OK;
    }
    return ERR_MEMORY; // No free frame was found
}

int put(lower_t* self, size_t order, pfn_t frame){
    (void) order; //TODO orders

    //chek iif outside of managed space
    if(frame >= self->start_pfn + self->length || frame < self->start_pfn) return ERR_CORRUPTION;
    size_t child_index = get_child_index(self, frame);
    size_t field_index = (frame - self->start_pfn) % FIELDSIZE;

    int ret = reset_Bit(&self->fields[child_index], field_index);
    if(ret != ERR_OK) return ret;

    ret = child_counter_inc(&self->childs[child_index]);
    assert(ret == ERR_OK); // should allways be ok
    return ret;
}

int is_free(lower_t* self, pfn_t frame, size_t order){
    (void) order; //TODO orders
    // check if outside of managed space
    if(frame >= self->start_pfn + self->length || frame < self->start_pfn) return ERR_CORRUPTION;
    size_t child_index = get_child_index(self, frame);
    size_t field_index = (frame - self->start_pfn) % FIELDSIZE;
    
    uint16_t child_counter = get_counter(&self->childs[child_index]);
    if(child_counter == 0) return false;

    return is_free_bit(&self->fields[child_index], field_index);
}

uint64_t allocated_frames(lower_t* self){
    uint64_t counter = self->length;
    for(size_t i = 0; i < num_of_childs(self); i++){
        //counter += count_Set_Bits(&self->fields[i]);
        counter -= get_counter(&self->childs[i]);
    }
    return counter;
};

void print_lower(lower_t* self){
    printf("lower allocator: with %zu childs\n%lu/%lu frames are allocated\n", num_of_childs(self),allocated_frames(self), self->length);

}


