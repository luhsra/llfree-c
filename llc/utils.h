#pragma once
#include <stdint.h>
#include <stddef.h>
#include "stdbool.h"

//TODO function descriptions

#define MAX_ATOMIC_RETRY 5 // Maximum retrys if a atomic operation has failed
// atomic mem order ist sequenziell
// alternativ ist acquire-release: memory_order_acq_rel -> siehe stdatomic.h
#define MEMORY_ORDER memory_order_seq_cst

size_t div_ceil(uint64_t a, int b);


#define cas(obj, expect, desire) atomic_compare_exchange_strong_explicit(obj, expect, desire, MEMORY_ORDER, MEMORY_ORDER)
#define load(obj) atomic_load_explicit(obj, MEMORY_ORDER)

//#define verbose

#ifdef verbose
    #define p(...) printf(__VA_ARGS__);
#else
    #define p(...) 
#endif