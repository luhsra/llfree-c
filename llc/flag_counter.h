#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <stdatomic.h>
#include "enum.h"

//TODO Nice Function Descriptions


// counter can be used for child and tree counter
typedef struct flag_counter{
    union{
        alignas(2) _Atomic(uint16_t) raw;
        struct{
            uint16_t counter:15;
            bool flag :1;
        };
    };
} flag_counter_t;

//initializes the flag and counter
// counter must be < 0x8000
flag_counter_t init_flag_counter(uint16_t counter, bool flag);

//sets the Flag. returns 0 for success and -2 if the flag was already set
int reserve_HP(flag_counter_t* self);

//resets the Flag. returns 0 for success and -2 if the flag was already reset
int free_HP(flag_counter_t* self);

//increments the counter. returns new counter value for success; -2 if atomic was interrupted; -1 if out of range
int atomic_counter_inc(flag_counter_t* self);

// checks if flag is not set and counter boundry bovore calling atomic_counter_inc
int child_counter_inc(flag_counter_t* self);

//decrements the counter. returns new counter value for success; -2 if atomic was interrupted; -1 if out of range
int atomic_counter_dec(flag_counter_t* self);

// checks if flag is not set and counter boundry bovore calling atomic_counter_dec
int child_counter_dec(flag_counter_t* self);


