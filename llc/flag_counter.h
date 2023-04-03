

#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

// counter can be used for child and tree counter
typedef struct flag_counter{
    union{
        struct{
            uint16_t counter:15;
            bool flag :1;
        };
        alignas(2) _Atomic(uint16_t) raw;
    };
} flag_counter_t;

//sets the Flag. returns 0 for success and -1 if the flag was already set
int atomic_flag_set(flag_counter_t* self);

//resets the Flag. returns 0 for success and -1 if the flag was already reset
int atomic_flag_reset(flag_counter_t* self);

//increments the counter. returns new counter value for success; -1 if atomic was interrupted; -2 if out of range
int atomic_counter_inc(flag_counter_t* self);

//decrements the counter. returns new counter value for success; -1 if atomic was interrupted; -2 if out of range
int atomic_counter_dec(flag_counter_t* self);


