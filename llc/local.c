#include "local.h"
#include "assert.h"
#include "enum.h"
#include "flag_counter.h"
#include <stdatomic.h>
#include <stdint.h>


void init_local(local_t* self){
    assert(self != NULL);
    self->last_free.free_counter = 0;
    self->last_free.last_free_idx = 0;
    self->reserved.fcounter = 0;
    self->reserved.in_reservation = false;
    self->reserved.preferred_index = MAX_TREE_INDEX;
}

int set_preferred(local_t* self, size_t pfn, flag_counter_t* tree_p){
    assert(self != NULL);

    reserved_t old = {atomic_load(&self->reserved.raw)};

    assert(old.in_reservation);

    flag_counter_t tree = {atomic_load(&tree_p->raw)};
    reserved_t new = {0};
    new.fcounter = tree.counter;
    new.preferred_index = pfn >> 6; // index of row in the bitfield 2^6 (64) einträge pro atomic field

    assert(new.preferred_index != MAX_TREE_INDEX);

    for(size_t i = 0; i < MAX_ATOMIC_RETRY; ++i){
        int ret = atomic_compare_exchange_strong(&self->reserved.raw, (uint64_t*) &old.raw, new.raw);
        if(ret) return ERR_OK;
    }

    return ERR_RETRY;
}



uint64_t get_reserved_tree_index(local_t* self){
    assert(self != NULL);

    reserved_t pref = {atomic_load(&self->reserved.raw)};
    return pref.preferred_index << 6;

}

bool has_reserved_tree(local_t* self){
    assert(self != NULL);
    reserved_t pref = {atomic_load(&self->reserved.raw)};

    return pref.preferred_index != MAX_TREE_INDEX;
}

int mark_as_searchig(local_t* self){
    assert(self != NULL);

    reserved_t old = {atomic_load(&self->reserved.raw)};
    for(size_t i = 0; i < MAX_ATOMIC_RETRY; ++i){
        if(old.in_reservation) return ERR_INITIALIZATION;
        reserved_t new = old;
        new.in_reservation = true;

        int ret = atomic_compare_exchange_strong(&self->reserved.raw, (uint64_t*) &old.raw, new.raw);
        if(ret) return ERR_OK;
    }
    return ERR_RETRY;

}

bool is_searching(local_t* self){
    assert(self != NULL);
    reserved_t pref = {atomic_load(&self->reserved.raw)};

    return pref.in_reservation;
}

int inc_free_counter(local_t* self, uint64_t pfn){
    assert(self != NULL);
    pfn = pfn >> 6;

    last_free_t old = {atomic_load(&self->last_free.raw)};
    for(size_t i = 0; i < MAX_ATOMIC_RETRY; ++i){
        last_free_t new = old;
        if(old.last_free_idx == pfn){
            ++new.free_counter;
        }else{
            new.free_counter = 0;
            new.last_free_idx = pfn;
        }
        int ret = atomic_compare_exchange_strong(&self->last_free.raw, (uint64_t*) &old.raw, new.raw);
        if(ret){ // success swap
            //if(old.free_counter >= 4) //TODO reserve this tree;
            return ERR_OK;;
        }
    }
    return ERR_RETRY;
}