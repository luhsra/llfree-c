#pragma once
#include "stdbool.h"
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

// TODO function descriptions
// order of a Hugepage

#define HP_ORDER 9
#define FRAME_SIZE (1ul << 12) // 4 KiB == 2^12

#define ATOMIC_SHIFT 6
#define CHILD_SHIFT 9
#define TREE_SHIFT 14

#define tree_from_pfn(_N) ({ (_N) >> TREE_SHIFT; })
#define pfn_from_tree(_N) ({ (_N) << TREE_SHIFT; })

#define child_from_pfn(_N) ({ (_N) >> CHILD_SHIFT; })
#define pfn_from_child(_N) ({ (_N) << CHILD_SHIFT; })

#define atomic_from_pfn(_N) ({ (_N) >> ATOMIC_SHIFT; })
#define pfn_from_atomic(_N) ({ (_N) << ATOMIC_SHIFT; })

#define tree_from_atomic(_N) ({ (_N) >> (TREE_SHIFT - ATOMIC_SHIFT); })

// Maximum retrys if a atomic operation has failed
#define MAX_ATOMIC_RETRY 5

// alternativ ist acquire-release: memory_order_seq_cst -> siehe stdatomic.h
#define MEMORY_LOAD_ORDER memory_order_acquire
#define MEMORY_STORE_ORDER memory_order_acq_rel

size_t div_ceil(uint64_t a, int b);

/**
 * @brief Itterates over a Range between multiples of len starting at idx.
 * Starting at idx up to the next Multiple of len (exclusive). Then the next
 * step will be the highest multiple of len less than idx. (_base_idx)
 * Loop will end after len itterations.
 * code will be executed in each loop.
 * The currend loop value can accesed by current_i
 *
 */
#define ITERRATE(idx, len, code)                                               \
  do {                                                                         \
    const size_t _offset = (idx) % (len);                                      \
    const size_t _base_idx = (idx)-_offset;                                    \
    for (size_t _i = 1; _i <= (len); ++_i) {                                   \
      int64_t toggle = _i & 1 ? _i / 2 : -_i / 2;                              \
      const size_t current_i = _base_idx + ((toggle + _offset) % (len));       \
      { code }                                                                 \
    }                                                                          \
  } while (false)

/**
 * @brief compare and swap wrapper for structs with a atomic 16 or 64 Bit raw
 * member
 * @param obj pointer to struct with atomic raw member.
 * @param expect pointer to struct with matching raw member
 * @param desite struct with matching raw member
 * @return ERR_OK on success
 *         ERR_RETRY of atomic operation failed
 */
#define cas(obj, expect, desire)                                               \
  ({                                                                           \
    int _ret = ERR_RETRY;                                                      \
    if (atomic_compare_exchange_weak_explicit(                                 \
            &(obj)->raw,                                                       \
            (_Generic(((obj)->raw),                                            \
            uint16_t: (uint16_t *)&(expect)->raw,                              \
            default: (uint64_t *)&(expect)->raw)),                             \
            (desire).raw, MEMORY_STORE_ORDER, MEMORY_LOAD_ORDER))              \
      _ret = ERR_OK;                                                           \
    _ret;                                                                      \
  })

/**
 * @brief wrapper for atomic load
 *
 */
#define load(obj) atomic_load_explicit(obj, MEMORY_LOAD_ORDER)

/**
 * @brief Executes an endless Loop until given Function returns a value !=
 * ERR_RETRY Used for atomic stores to try until the cas succseed.
 * @return return value of given function. (never ERR_RETRY)
 */
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

/**
 * @brief Executes given function up to MAX_ATOMIC_RETRY times or until return
 * value != ERR_RETRY
 * @return return value of given function. (could be ERR_RETRY)
 */
#define try_update(func)                                                       \
  ({                                                                           \
    int _ret;                                                                  \
    for (size_t _i_ = 0; _i_ < MAX_ATOMIC_RETRY; ++_i_) {                      \
      _ret = func;                                                             \
      if (_ret != ERR_RETRY)                                                   \
        break;                                                                 \
    }                                                                          \
    _ret;                                                                      \
  })

// #define verbose

#ifdef verbose
#define p(...) printf(__VA_ARGS__)
#else
#define p(...)
#endif