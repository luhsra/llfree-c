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
#include "utils.h"

#include <stdio.h>


/**
 * helper to get the childindex from the pfn
 */
size_t get_child_index(pfn_rt pfn){
    return pfn / FIELDSIZE;
}

void init_default(lower_t* self, pfn_at start_pfn, size_t len){
    self->start_pfn = start_pfn;
    self->length = len;

    self->num_of_childs = div_ceil(self->length, FIELDSIZE);
    self->fields = malloc(sizeof(bitfield_512_t) * self->num_of_childs);
    assert(self->fields != NULL);
    self->childs = malloc(sizeof(flag_counter_t) * self->num_of_childs);
    assert(self->childs != NULL);   
}

int init_lower(lower_t* self, pfn_at start_pfn, size_t len, bool free_all){
    assert(self != NULL);

    self->start_pfn = start_pfn;
    self->length = len;

    for(size_t i = 0; i < self->num_of_childs -1; i++){
        self->fields[i] = init_field(0, free_all);
        self->childs[i]= init_flag_counter(free_all ? 0: FIELDSIZE, false);
    }
    size_t frames_in_last_field = self->length % FIELDSIZE;
    self->fields[self->num_of_childs -1] = init_field(frames_in_last_field, free_all);
    
    if(frames_in_last_field == 0) frames_in_last_field = FIELDSIZE;
    self->childs[self->num_of_childs -1] = init_flag_counter(free_all ? 0 :frames_in_last_field, false);
    return ERR_OK;
}


int lower_get(lower_t* self, pfn_rt start, size_t order, pfn_rt* ret){
    (void) order; //TODO Different Orders
    size_t index_start = get_child_index(start);
    index_start /= CHILDS_PER_TREE; //allways search in a chunk of 32 childs
    size_t index_end = index_start + 32;
    if(index_end > self->num_of_childs) index_end = self->num_of_childs;

    for(size_t index = index_start;index < index_end; index++){
        uint16_t child_counter = get_counter(&self->childs[index]);
        if(child_counter <= 0) continue;

        int result = child_counter_dec(&self->childs[index]);
        if(result != ERR_OK) continue; //if atomic failed multiple times or counter reaches 0 try next child

        int pos;
        do{ //the atomic counter decrease asures that here must be a free bit left in the field
            pos = set_Bit(&self->fields[index]);
        } while (pos < 0);

        *ret = index * FIELDSIZE + pos + self->start_pfn;
        return ERR_OK;
    }
    return ERR_MEMORY; // No free frame was found
}

int lower_put(lower_t* self, pfn_rt frame, size_t order){
    (void) order; //TODO orders

    //chek if outside of managed space
    if(frame >= self->start_pfn + self->length || frame < self->start_pfn) return ERR_ADDRESS;
    size_t child_index = get_child_index(frame);
    size_t field_index = (frame - self->start_pfn) % FIELDSIZE;

    int ret = reset_Bit(&self->fields[child_index], field_index);
    if(ret != ERR_OK) return ERR_ADDRESS;

    ret = child_counter_inc(&self->childs[child_index]);
    assert(ret == ERR_OK); // should allways be ok
    return ERR_OK;
}

int is_free(lower_t* self, pfn_rt frame, size_t order){
    (void) order; //TODO orders
    // check if outside of managed space
    if(frame >= self->start_pfn + self->length || frame < self->start_pfn) return ERR_ADDRESS;
    size_t child_index = get_child_index(frame);
    size_t field_index = (frame - self->start_pfn) % FIELDSIZE;
    
    uint16_t child_counter = get_counter(&self->childs[child_index]);
    if(child_counter == 0) return false;

    return is_free_bit(&self->fields[child_index], field_index);
}

size_t allocated_frames(lower_t* self){
    size_t counter = self->length;
    for(size_t i = 0; i < self->num_of_childs; i++){
        counter -= get_counter(&self->childs[i]);
    }
    return counter;
};

void print_lower(lower_t* self){
    printf("lower allocator: with %zu childs\n%lu/%lu frames are allocated\n", self->num_of_childs,allocated_frames(self), self->length);

}


