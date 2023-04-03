#include "flag_counter.h"


//sets the Flag. returns 0 for success and -1 if the flag was already set
int atomic_flag_set(flag_counter_t* self){
    (void) self;
    return -1;
}

//resets the Flag. returns 0 for success and -1 if the flag was already reset
int atomic_flag_reset(flag_counter_t* self){
    (void) self;
    return -1;
}

//increments the counter. returns new counter value for success; -1 if atomic was interrupted; -2 if out of range
int atomic_counter_inc(flag_counter_t* self){
    (void) self;
    return -1;
}

//decrements the counter. returns new counter value for success; -1 if atomic was interrupted; -2 if out of range
int atomic_counter_dec(flag_counter_t* self){
    (void) self;
    return -1;
}

