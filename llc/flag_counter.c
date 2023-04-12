#include "flag_counter.h"
#include "bitfield.h"
#include "enum.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>

#define MAX_ATOMIC_RETRY 5

//initializes the flag and counter
flag_counter_t init_flag_counter(uint16_t counter, bool flag){
    assert(counter < 0x8000);
    flag_counter_t ret;
    ret.flag = flag;
    ret.counter = counter;
    return ret;
}

static int cas(flag_counter_t* self, uint16_t* expect, uint16_t desire){
    for(size_t i = 0; i < MAX_ATOMIC_RETRY; i++){
        int ret = atomic_compare_exchange_strong(&self->raw, expect, desire);
        if(ret) return ERR_OK;
    }
    return ERR_RETRY;
}


//sets the Flag. returns 0 for success and -1 if the flag was already set
int reserve_HP(flag_counter_t* self){
    flag_counter_t old = {atomic_load(&self->raw)};
    if(old.counter != 512 || old.flag == true){
        return ERR_MEMORY;
    }

    uint16_t expect = 0x0100; // flag not set and all (512) Frames Free
    uint16_t desire = 0x8000;  // flag is set and counter = 0

    int ret = cas(self, &expect, desire);
    if(ret == ERR_OK){
        assert(self->flag == true);
        assert(self->counter == 0);
    }
    return ret;
}

//resets the Flag. returns 0 for success and -1 if the flag was already reset
int free_HP(flag_counter_t* self){
    flag_counter_t old = {atomic_load(&self->raw)};
    if(old.flag == false || old.counter != 0){
        return ERR_CORRUPTION;
    }

    uint16_t expect = 0x8000; // flag is set and counter = 0
    uint16_t desire = 0x0200;  // flag not set and all (512) Frames Free

    int faliure = cas(self, &expect, desire);

    if(faliure) {  //should never be a competiton for freeing a Huge-Page
        return ERR_CORRUPTION; 
    }
    
    assert(self->flag == false);
    assert(self->counter == FIELDSIZE);
    return ERR_OK;
}

//increments the counter. returns new counter value for success; -1 if atomic was interrupted; -2 if out of range
int atomic_counter_inc(flag_counter_t* self){

    uint16_t expect = atomic_load(&self->raw);
    flag_counter_t old = {expect};
    if(old.counter >= 0x7fff) return ERR_MEMORY;
    old.counter++;
    uint16_t desired = old.raw;
    int faliure =  cas(self, &expect, desired);

    return faliure;
}

int child_counter_inc(flag_counter_t* self){
    flag_counter_t old = {atomic_load(&self->raw)};
    if(old.flag == true || old.counter >= FIELDSIZE) return ERR_CORRUPTION;
    return atomic_counter_inc(self);
}

//decrements the counter. returns new counter value for success; -1 if atomic was interrupted; -2 if out of range
int atomic_counter_dec(flag_counter_t* self){
    uint16_t expect = atomic_load(&self->raw);
    flag_counter_t old = {expect};
    if(old.counter <= 0){
        return ERR_CORRUPTION; // counter should not be able to go negative
    }

    old.counter--;
    uint16_t desired = old.raw;
    int faliure =  cas(self, &expect, desired);

    return faliure;
}

int child_counter_dec(flag_counter_t* self){
    flag_counter_t old = {atomic_load(&self->raw)};
    if(old.flag == true) return ERR_CORRUPTION;
    return atomic_counter_dec(self);
}

