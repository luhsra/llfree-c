#pragma once
#include "stdbool.h"
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

// TODO function descriptions

#define MAX_ATOMIC_RETRY 5 // Maximum retrys if a atomic operation has failed
// atomic mem order ist sequenziell
// alternativ ist acquire-release: memory_order_acq_rel -> siehe stdatomic.h
#define MEMORY_ORDER memory_order_seq_cst

size_t div_ceil(uint64_t a, int b);

#define cas(obj, expect, desire)                                               \
  atomic_compare_exchange_weak_explicit(obj, expect, desire, MEMORY_ORDER,     \
                                        MEMORY_ORDER)
#define load(obj) atomic_load_explicit(obj, MEMORY_ORDER)


// var desire must be must be the same type as *obj
// returns only if cas was a success
#define fetch_update(obj, desire, code)                                                \
  ({                                                                           \
    typeof(*obj) old = {load(&(obj)->raw)};                                    \
    while (true) {                                                             \
      code;                                                                    \
      if (cas(&(obj)->raw,                                                     \
              _Generic(((obj)->raw),                                           \
              uint16_t: (uint16_t *)&(old).raw,                                \
              default: (uint64_t *)&(old).raw),                                \
              (desire).raw))                                                     \
        break;                                                                 \
    }                                                                          \
    (typeof(*obj))old;                                                         \
  })

// #define verbose

#ifdef verbose
#define p(...) printf(__VA_ARGS__)
#else
#define p(...)
#endif