#include "lower.h"
#include "bitfield.h"
#include "flag_counter.h"
#include "pfn.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include <stdio.h>


static size_t div_ceil(uint64_t a, int b){
    //wenn es einen Rest gibt muss aufgerundet werden
    return a % b ? a / b + 1 : a / b;
}

static size_t num_of_childs(lower_t* self){
    return div_ceil(self->length, FIELDSIZE);
}



lower_t* init_default(pfn_t start_pfn, uint64_t len, bool free_all){
    lower_t* self = malloc(sizeof(lower_t));
    assert(self != NULL);

    self->start_pfn = start_pfn;
    self->length = len;

    size_t childnumber = num_of_childs(self);
    self->fields = malloc(sizeof(bitfield_512_t) * childnumber);
    assert(self->fields != NULL);
    self->childs = malloc(sizeof(flag_counter_t) * childnumber);
    assert(self->childs != NULL);


    for(size_t i = 0; i < childnumber -1; i++){
        self->fields[i] = init_field(512, free_all);
        self->childs[i]= init_flag_counter(free_all ? 0: 512, false);
    }
    self->fields[childnumber -1] = init_field(self->length%512, free_all);
    self->childs[childnumber -1] = init_flag_counter(self->length%512, false);

    return self;
}

int init_lower(lower_t* self, pfn_t start_pfn, uint64_t len, bool free_all){
    (void) self;
    (void) start_pfn;
    (void) len;
    (void) free_all; 

    return -1;
}

static size_t get_child_index(pfn_t pfn){
    return div_ceil(pfn, FIELDSIZE);
}

int get(lower_t* self, range_t range, size_t order, pfn_t* ret){
    (void) order;
    size_t index = get_child_index(range.start);
    size_t index_end = get_child_index(range.end);

    uint64_t pos;
    for(;index < index_end; index++){
        if(self->childs[index].counter > 0){
            if(find_unset(&self->fields[index], &pos) == 0) break;
        }
    }
    if(index == index_end) return -1; // No free frame was found

    int result = atomic_counter_inc(&self->childs[index]);
    assert(result != -1 && "must never be out of range");

    //TODO childcounter muss wieder decrementiert werden
    if(result == -2) return -2; // if atomic failed return 

    result = set_Bit(&self->fields[index], pos);

    if(result != 0) return result;

    *ret = index * FIELDSIZE + pos;
    return 0;
}

int put(lower_t* self, pfn_t frame, size_t order);

int is_free(lower_t* self, pfn_t frame, size_t order);

uint64_t allocated_frames(lower_t* self){
    uint64_t counter = 0;
    for(size_t i = 0; i < num_of_childs(self); i++){
        counter += count_Set_Bits(&self->fields[i]);
        //counter+= self.childs->counter;
    }
    return counter;
};

void print_lower(lower_t* self){
    printf("lower allocator: with %zu childs\n%lu/%lu frames are allocated\n", num_of_childs(self),allocated_frames(self), self->length);

}


