#include "flag_counter.h"
#include "bitfield.h"
#include "enum.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>



/**
 * @brief Wrapper-function for a compare_and_swap operation on a flagcounter
 * 
 * @param self pointer to the acessed flagcounter
 * @param expect expected Value of the flagcounter (raw)
 * @param desire the new wanted Value
 */
static int cas(flag_counter_t* self, uint16_t* expect, uint16_t desire){
    assert(self != NULL);
    assert(expect != NULL);

    for(size_t i = 0; i < MAX_ATOMIC_RETRY; i++){
        int ret = atomic_compare_exchange_strong(&self->raw, expect, desire);
        if(ret) return ERR_OK;
    }
    return ERR_RETRY;
}

flag_counter_t init_flag_counter(uint16_t counter, bool flag){
    assert(counter < 0x8000); // max limit for 15 bit
    flag_counter_t ret;
    ret.flag = flag;
    ret.counter = counter;
    return ret;
}

int reserve_HP(flag_counter_t* self){
    assert(self != NULL);

    flag_counter_t old = {atomic_load(&self->raw)};
    if(old.counter != 512 || old.flag == true){
        return ERR_MEMORY;
    }

    uint16_t expect = 0x0100; // flag not set and all (512) Frames Free
    uint16_t desire = 0x8000;  // flag is set and counter = 0

    return cas(self, &expect, desire);
}

int free_HP(flag_counter_t* self){
    flag_counter_t old = {atomic_load(&self->raw)};
    if(old.flag == false || old.counter != 0){
        return ERR_ADDRESS;
    }

    uint16_t expect = 0x8000; // flag is set and counter = 0
    uint16_t desire = 0x0200;  // flag not set and all (512) Frames Free

    int ret = cas(self, &expect, desire);

    assert(ret != ERR_RETRY);
    return ret;
}

int atomic_counter_inc(flag_counter_t* self){

    uint16_t expect = atomic_load(&self->raw);
    flag_counter_t old = {expect};
    if(old.counter >= 0x7fff) return ERR_MEMORY;
    old.counter++;
    uint16_t desired = old.raw;
    int ret =  cas(self, &expect, desired); //TODO cas klappt hier nicht mit den retrys, da desired nicht mitverändert wird

    return ret;
}

int child_counter_inc(flag_counter_t* self){
    flag_counter_t old = {atomic_load(&self->raw)};
    if(old.flag == true) return ERR_ADDRESS;
    return atomic_counter_inc(self);
}

int atomic_counter_dec(flag_counter_t* self){
    uint16_t expect = atomic_load(&self->raw);
    flag_counter_t old = {expect};
    if(old.counter <= 0){
        return ERR_MEMORY; // counter should not be able to go negative
    }

    old.counter--;
    uint16_t desired = old.raw;
    int ret =  cas(self, &expect, desired); //TODO cas klappt hier nicht mit den retrys, da desired nicht mitverändert wird

    return ret;
}

int child_counter_dec(flag_counter_t* self){
    flag_counter_t old = {atomic_load(&self->raw)};
    if(old.flag == true) return ERR_ADDRESS;
    return atomic_counter_dec(self);
}


uint16_t get_counter(flag_counter_t* self){
    return atomic_load(&self->raw);
}
