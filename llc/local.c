#include "local.h"
#include "assert.h"
#include "enum.h"
#include "child.h"
#include "pfn.h"
#include "utils.h"
#include <stdatomic.h>
#include <stddef.h>
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

int set_preferred(local_t* self, pfn_rt pfn, uint16_t free_count, reserved_t* old_reservation) {
    assert(self != NULL);
    assert(free_count < 0x8000);

    pfn = pfn >> 6;
    assert(pfn < 0x400000000000); // if fits in the 46 bit storage

    reserved_t old = {atomic_load(&self->reserved.raw)};
    assert(old.reservation_in_progress);

    reserved_t new = {0};
    new.free_counter = free_count;
    new.preferred_index = pfn;
    new.has_reserved_tree = true;

    for(size_t i = 0; i < MAX_ATOMIC_RETRY; ++i){
        int ret = cas(&self->reserved.raw, (uint64_t*) &old.raw, new.raw);
        if(ret) {
            *old_reservation = old;
            return ERR_OK;
        }
    }

    return ERR_RETRY;
}



pfn_rt get_reserved_tree_index(local_t* self){
    assert(self != NULL);

    reserved_t pref = {atomic_load(&self->reserved.raw)};
    return pref.preferred_index << 6;

}

pfn_rt get_last_free_tree_index(local_t* self){
    assert(self != NULL);

    last_free_t pref = {atomic_load(&self->last_free.raw)};
    return pref.last_free_idx << 6;

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

int inc_free_counter(local_t* self, pfn_rt frame, size_t order){
    assert(self != NULL);
    size_t atomic_Idx  = getAtomicIdx(frame);
    assert(atomic_Idx < 0x400000000000); // if fits in the 46 bit storage

    (void) (order); //TODO order
    reserved_t old = {load(&self->reserved.raw)};
    for(size_t i = 0; i < MAX_ATOMIC_RETRY; ++i){
        if(old.preferred_index != atomic_Idx){
            // reserved tree is not the a match for given pfn
            return ERR_ADDRESS;
        }

        reserved_t new = old;
        new.free_counter += 1;
        int ret = cas(&self->reserved.raw, (uint64_t*) &old.raw, new.raw);
        if(ret){
            return ERR_OK;
        }
    }
    return ERR_RETRY;
}


int set_free_tree(local_t* self, pfn_rt frame){
    assert(self != NULL);
    size_t atomic_Idx  = getAtomicIdx(frame);
    assert(atomic_Idx < 0x400000000000); // if fits in the 46 bit storage


    last_free_t old = {load(&self->last_free.raw)};

    for(size_t i = 0; i < MAX_ATOMIC_RETRY; ++i){
        last_free_t new = {0};
        if(old.last_free_idx != atomic_Idx){
            new.last_free_idx = atomic_Idx;
            new.free_counter = 0;
        } else if (old.free_counter < 3){
            new = old;
            new.free_counter += 1;
        } else{
            // in this tree were 4 consecutive frees
            // -> no change and message to reserve this tree
            return ERR_MEMORY;
        }

        int ret = cas(&self->last_free.raw, (uint64_t*) &old.raw, new.raw);
        if(ret){
            // successfully increased counter of set last to new tree
            return ERR_OK;
        }
    }
    return ERR_RETRY;
}