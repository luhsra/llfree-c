#include "local.h"
#include "assert.h"
#include "enum.h"
#include "child.h"
#include "utils.h"
#include <stdatomic.h>
#include <stdint.h>


void init_local(local_t* self){
    assert(self != NULL);
    self->last_free.free_counter = 0;
    self->last_free.last_free_idx = 0;
    self->reserved.free_counter = 0;
    self->reserved.preferred_index = 0;
    self->reserved.reservation_in_progress = false;
    self->reserved.has_reserved_tree = false;
}

int set_preferred(local_t* self, pfn_rt pfn, uint16_t free_count){
    assert(self != NULL);
    assert(free_count < 0x8000);

    pfn = pfn >> 6;
    assert(pfn < 0x400000000000); // if fits in the 46 bit storage

    reserved_t old = {atomic_load(&self->reserved.raw)};
    assert(old.reservation_in_progress);

    reserved_t new = {0};
    new.free_counter = free_count;
    new.preferred_index = pfn;

    for(size_t i = 0; i < MAX_ATOMIC_RETRY; ++i){
        int ret = cas(&self->reserved.raw, (uint64_t*) &old.raw, new.raw);
        if(ret) return ERR_OK;
    }

    return ERR_RETRY;
}



pfn_rt get_reserved_tree_index(local_t* self){
    assert(self != NULL);

    reserved_t pref = {atomic_load(&self->reserved.raw)};
    return pref.preferred_index << 6;

}

bool has_reserved_tree(local_t* self){
    assert(self != NULL);
    reserved_t pref = {atomic_load(&self->reserved.raw)};

    return pref.reservation_in_progress;
}



int mark_as_searchig(local_t* self){
    assert(self != NULL);

    reserved_t mask = {0ul};
    mask.reservation_in_progress = true;

    uint64_t before = atomic_fetch_or(&self->reserved.raw, mask.raw);
    if((before | mask.raw) == before) return ERR_RETRY; //no change means flag was already set
    return ERR_OK;
}

bool is_searching(local_t* self){
    assert(self != NULL);
    reserved_t pref = {atomic_load(&self->reserved.raw)};

    return pref.reservation_in_progress;
}

int inc_free_counter(local_t* self, pfn_rt pfn){
    assert(self != NULL);
    pfn = pfn >> 6;
    assert(pfn < 0x400000000000); // if fits in the 46 bit storage


    last_free_t old = {atomic_load(&self->last_free.raw)};
    for(size_t i = 0; i < MAX_ATOMIC_RETRY; ++i){
        last_free_t new = old;
        if(old.last_free_idx == pfn){
            ++new.free_counter;
        }else{
            new.free_counter = 0;
            new.last_free_idx = pfn;
        }
        int ret = cas(&self->last_free.raw, (pfn_at*) &old.raw, new.raw);
        if(ret){ // success swap
            //if(old.free_counter >= 4) //TODO reserve this tree;
            return ERR_OK;;
        }
    }
    return ERR_RETRY;
}