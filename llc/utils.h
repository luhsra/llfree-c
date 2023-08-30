#pragma once

#include "stdbool.h"
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

static const size_t FRAME_BITS = 12;
static const size_t FRAME_SIZE = 1 << FRAME_BITS;

// order of a Huge frame
static const size_t HP_ORDER = 9;

// maximum order that can be allocated
static const size_t MAX_ORDER = HP_ORDER;

static const size_t ATOMIC_SHIFT = 6;
static const size_t CHILD_SHIFT = 9;
static const size_t TREE_SHIFT = 14;

#define tree_from_pfn(_N) ({ (_N) >> TREE_SHIFT; })
#define pfn_from_tree(_N) ({ (_N) << TREE_SHIFT; })

#define child_from_pfn(_N) ({ (_N) >> CHILD_SHIFT; })
#define pfn_from_child(_N) ({ (_N) << CHILD_SHIFT; })

#define atomic_from_pfn(_N) ({ (_N) >> ATOMIC_SHIFT; })
#define pfn_from_atomic(_N) ({ (_N) << ATOMIC_SHIFT; })

#define tree_from_atomic(_N) ({ (_N) >> (TREE_SHIFT - ATOMIC_SHIFT); })

// Maximum amount of retry if a atomic operation has failed
static const size_t MAX_ATOMIC_RETRY = 5;

// alternative is acquire-release: memory_order_seq_cst -> more at stdatomic.h
static const int MEMORY_LOAD_ORDER = memory_order_acquire;
static const int MEMORY_STORE_ORDER = memory_order_acq_rel;

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static inline size_t div_ceil(uint64_t a, int b)
{
	return (a + b - 1) / b;
}

/**
 * @brief Iterates over a Range between multiples of len starting at idx.
 * Starting at idx up to the next Multiple of len (exclusive). Then the next
 * step will be the highest multiple of len less than idx. (_base_idx)
 * Loop will end after len iterations.
 * code will be executed in each loop.
 * The current loop value can accessed by current_i
 *
 */
#define for_offsetted(idx, len)                                   \
	for (size_t _i = 0, _offset = (idx) % (len),              \
		    _base_idx = (idx)-_offset, current_i = (idx); \
	     _i < (len);                                          \
	     _i = _i + 1, current_i = _base_idx + ((_i + _offset) % (len)))

/**
 * @brief compare and swap wrapper for structs with a atomic 16 or 64 Bit raw
 * member
 * @param obj pointer to struct with atomic raw member.
 * @param expect pointer to struct with matching raw member
 * @param desire struct with matching raw member
 * @return ERR_OK on success
 *         ERR_RETRY of atomic operation failed
 */
#define cas(obj, expect, desire)                                            \
	({                                                                  \
		int _ret = ERR_RETRY;                                       \
		if (atomic_compare_exchange_weak_explicit(                  \
			    &(obj)->raw,                                    \
			    (_Generic(((obj)->raw), uint16_t                \
				      : (uint16_t *)&(expect)->raw, default \
				      : (uint64_t *)&(expect)->raw)),       \
			    (desire).raw, MEMORY_STORE_ORDER,               \
			    MEMORY_LOAD_ORDER))                             \
			_ret = ERR_OK;                                      \
		_ret;                                                       \
	})

/**
 * @brief wrapper for atomic load
 *
 */
#define load(obj) atomic_load_explicit(obj, MEMORY_LOAD_ORDER)

/**
 * @brief Executes an endless Loop until given Function returns a value !=
 * ERR_RETRY Used for atomic stores to try until the cas succeed.
 * @return return value of given function. (never ERR_RETRY)
 */
#define update(func)                           \
	({                                     \
		int _ret;                      \
		while (true) {                 \
			_ret = func;           \
			if (_ret != ERR_RETRY) \
				break;         \
		}                              \
		_ret;                          \
	})

/**
 * @brief Atomic fetch-modify-update macro.
 *
 * This macro loads the value at `atom_ptr`, stores its result in `old_val`
 * and then executes the `code` which is expected to modify `value`,
 * which is then stored atomically with CAS.
 *
 * @return If the update was successfull. Fails only if `code` returns false.
 *
 * Example:
 * ```
 * _Atomic uint64_t my_atomic;
 * uint64_t old;
 * if (!fetch_update(&my_atomic, old, ({
 * 	    value *= value;
 * 	    true;
 *     }))) {
 * 	assert(!"here this should never fail!");
 * }
 * printf("old value %lu\n", old);
 * ```
 */
#define fetch_update(atom_ptr, old_val, code)                                 \
	({                                                                    \
		bool _ret = false;                                            \
		(old_val) = load(atom_ptr);                                   \
		while (true) {                                                \
			typeof(old_val) value = (old_val);                    \
			bool _succ = (code);                                  \
			if (!_succ)                                           \
				break;                                        \
			if (atomic_compare_exchange_weak_explicit(            \
				    (atom_ptr), &(old_val), value,            \
				    MEMORY_STORE_ORDER, MEMORY_LOAD_ORDER)) { \
				_ret = true;                                  \
				break;                                        \
			}                                                     \
		}                                                             \
		_ret;                                                         \
	})

/**
 * @brief Executes given function up to MAX_ATOMIC_RETRY times or until return
 * value != ERR_RETRY
 * @return return value of given function. (could be ERR_RETRY)
 */
#define try_update(func)                                              \
	({                                                            \
		int _ret;                                             \
		for (size_t _i_ = 0; _i_ < MAX_ATOMIC_RETRY; ++_i_) { \
			_ret = func;                                  \
			if (_ret != ERR_RETRY)                        \
				break;                                \
		}                                                     \
		_ret;                                                 \
	})

// #define verbose

#ifdef verbose
#define p(...) printf(__VA_ARGS__)
#else
#define p(...)
#endif
