#pragma once
#include "stdbool.h"
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

// TODO function descriptions
// order of a Hugepage
#define HP 9

#define MAX_ATOMIC_RETRY 5 // Maximum retrys if a atomic operation has failed
// atomic mem order ist sequenziell
// alternativ ist acquire-release: memory_order_acq_rel -> siehe stdatomic.h
#define MEMORY_LOAD_ORDER memory_order_seq_cst
#define MEMORY_STORE_ORDER memory_order_seq_cst

size_t div_ceil(uint64_t a, int b);

#define cas(obj, expect, desire)                                               \
  ({                                                                           \
    int _ret = ERR_RETRY;                                                      \
    if (atomic_compare_exchange_weak_explicit(                                 \
            &(obj)->raw,                                                       \
            (_Generic(((obj)->raw),                                            \
            uint16_t: (uint16_t *)&(expect).raw,                               \
            default: (uint64_t *)&(expect).raw)),                              \
            (desire).raw, MEMORY_STORE_ORDER, MEMORY_LOAD_ORDER))              \
      _ret = ERR_OK;                                                           \
    _ret;                                                                      \
  })

#define load(obj) atomic_load_explicit(obj, MEMORY_LOAD_ORDER)

// var desire must be must be the same type as *obj
// returns only if cas was a success
#define fetch_update(obj, desire, code)                                        \
  ({                                                                           \
    typeof(*obj) old = {load(&(obj)->raw)};                                    \
    while (true) {                                                             \
      code;                                                                    \
      if (cas(&(obj)->raw,                                                     \
              _Generic(((obj)->raw),                                           \
              uint16_t: (uint16_t *)&(old).raw,                                \
              default: (uint64_t *)&(old).raw),                                \
              (desire).raw))                                                   \
        break;                                                                 \
    }                                                                          \
    (typeof(*obj))old;                                                         \
  })

#define update(func)                                                           \
  ({                                                                           \
    int _ret;                                                                  \
    while (true) {                                                             \
      _ret = func;                                                             \
      if (_ret != ERR_RETRY)                                                   \
        break;                                                                 \
    }                                                                          \
    _ret;                                                                      \
  })

#define try_update(func)                                                       \
  ({                                                                           \
    int _ret;                                                                  \
    for (size_t i = 0; i < MAX_ATOMIC_RETRY; ++i) {                            \
      _ret = func;                                                             \
      if (_ret != ERR_RETRY)                                                   \
        break;                                                                 \
    }                                                                          \
    _ret;                                                                      \
  })

//#define verbose

#ifdef verbose
#define p(...) printf(__VA_ARGS__)
#else
#define p(...)
#endif