#include "flag_counter.h"
#include "bitfield.h"
#include "enum.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include "utils.h"



flag_counter_t init_flag_counter(uint16_t counter, bool flag){
    assert(counter < 0x8000); // max limit for 15 bit
    flag_counter_t ret;
    ret.flag = flag;
    ret.counter = counter;
    return ret;
}


static bool update_reserve_HP(uint16_t* expect, uint16_t* desire){
    flag_counter_t old = {*expect};
    if(old.flag == true || old.counter != FIELDSIZE) return false;
    flag_counter_t desired;
    desired.counter = 0;
    desired.flag = true;
    *desire = desired.raw;
    return true;
}

int reserve_HP(flag_counter_t* self){
    assert(self != NULL);
    return cas(&self->raw, update_reserve_HP);
}

static bool update_free_HP(uint16_t* expect, uint16_t* desire){
    flag_counter_t old = {*expect};
    if(old.flag == false || old.counter != 0) return false;
    flag_counter_t desired;
    desired.counter = FIELDSIZE;
    desired.flag = false;
    *desire = desired.raw;
    return true;
}

int free_HP(flag_counter_t* self){
    assert(self != NULL);
    return cas(&self->raw, update_free_HP);
}


static bool update_inc_child(uint16_t* expect, uint16_t* desire){
    flag_counter_t old = {*expect};
    if(old.counter >= 0x7FFF) return false;
    ++old.counter;
    *desire = old.raw;
    return true;
}

int atomic_counter_inc(flag_counter_t* self){
    assert(self != NULL);

    return cas(&self->raw, update_inc_child);
}

int child_counter_inc(flag_counter_t* self){
    assert(self != NULL);

    flag_counter_t old = {atomic_load(&self->raw)};
    if(old.flag == true) return ERR_ADDRESS;
    
    assert(old.counter <= FIELDSIZE);
    return atomic_counter_inc(self);
}


static bool update_dec_child(uint16_t* expect, uint16_t* desire){
    flag_counter_t old = {*expect};
    if(old.counter == 0) return false;
    --old.counter;
    *desire = old.raw;
    return true;
}

int atomic_counter_dec(flag_counter_t* self){
    assert(self != NULL);
    return cas(&self->raw, update_dec_child);
}

int child_counter_dec(flag_counter_t* self){
    flag_counter_t old = {atomic_load(&self->raw)};
    if(old.flag == true) return ERR_ADDRESS;
    return atomic_counter_dec(self);
}


uint16_t get_counter(flag_counter_t* self){
    return atomic_load(&self->raw);
}
