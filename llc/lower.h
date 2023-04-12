#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "flag_counter.h"
#include "bitfield.h"
#include "pfn.h"

//TODO Nice Function Descriptions

typedef struct lower{
    pfn_t start_pfn;
    uint64_t length;
    bitfield_512_t* fields;
    flag_counter_t* childs;
}lower_t;


// the default allocates only memory
lower_t* init_default(pfn_t start_pfn, uint64_t len);


int init_lower(lower_t* self, pfn_t start_pfn, uint64_t len, bool free_all);

//ret will be set to a pfn of a newly allocated frame whitch will be allocated after start; returns -1 if not enough memory; -2 if atomic operation failed
int get(lower_t* self, size_t start, size_t order, pfn_t* ret);

//will free the given frame returns; -1 on error;  -2 if atomic operation failed
int put(lower_t* self, pfn_t frame, size_t order);

//checks if the frame is not allocated
int is_free(lower_t* self, pfn_t frame, size_t order);

//rerturns the number of currently allocated frames
uint64_t allocated_frames(lower_t* self);

//helper to print some stats about the allocator state
void print_lower(lower_t* self);

