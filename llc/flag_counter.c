#include "flag_counter.h"
#include <assert.h>


//initializes the flag and counter
flag_counter_t init_flag_counter(uint16_t counter, bool flag){
    assert(counter < 0x8000);
    return (flag_counter_t) {{{counter,flag}}};
}


//sets the Flag. returns 0 for success and -1 if the flag was already set
int atomic_flag_set(flag_counter_t* self){
    uint16_t mask = 0x8000;

    uint16_t before = __c11_atomic_fetch_or(&self->raw, mask, __ATOMIC_ACQ_REL);
    if((before | mask) == before){
        return -2;
    }
    return 0;
}

//resets the Flag. returns 0 for success and -1 if the flag was already reset
int atomic_flag_reset(flag_counter_t* self){
    uint16_t mask = 0x7fff;

    uint16_t before = __c11_atomic_fetch_and(&self->raw, mask, __ATOMIC_ACQ_REL);
    if((before & mask) == before){
        return -2;
    }
    return 0;
}

//increments the counter. returns new counter value for success; -1 if atomic was interrupted; -2 if out of range
int atomic_counter_inc(flag_counter_t* self){
    uint16_t mask = 0x7fff; //only flag bit is 0

    uint16_t expected = self->raw;

    // 0x7fff ist der max counter; -2 für out of range
    if(0x7fff <= (expected & mask)) return -1;

    uint16_t desired = expected + 1;
    bool success =  __c11_atomic_compare_exchange_strong (&self->raw, &expected, desired, false, __ATOMIC_ACQ_REL);
    if(success) return 0;
    return -2;
}

//decrements the counter. returns new counter value for success; -1 if atomic was interrupted; -2 if out of range
int atomic_counter_dec(flag_counter_t* self){
    uint16_t mask = 0x7fff; //only flag bit is 0

    uint16_t expected = self->raw;

    if((expected & mask) <= 0) return -1;

    uint16_t desired = expected - 1;
    bool success =  __c11_atomic_compare_exchange_strong (&self->raw, &expected, desired, false, __ATOMIC_ACQ_REL);
    if(success) return 0;
    return -2;
}

