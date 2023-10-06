#pragma once

#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>
#include <assert.h>
#include <stdio.h>

/// Unused functions and variables
#define _unused __attribute__((unused))

static const size_t FRAME_BITS = 12;
static const size_t FRAME_SIZE = 1 << FRAME_BITS;

/// Order of a huge frame
static const size_t HP_ORDER = 9;

/// Maximum order that can be allocated
static const size_t MAX_ORDER = HP_ORDER + 1;

static const size_t ATOMIC_SHIFT = 6;
static const size_t CHILD_SHIFT = 9;
static const size_t TREE_SHIFT = 14;

/// Minimal size the LLFree can manage
static const size_t MIN_PAGES = 1ul << MAX_ORDER;
/// 64 Bit Addresses - 12 Bit needed for offset inside the Page
static const size_t MAX_PAGES = 1ul << (64 - FRAME_BITS);

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))

static inline _unused size_t div_ceil(uint64_t a, int b)
{
	return (a + b - 1) / b;
}
static inline _unused int trailing_zeros(uint64_t val)
{
	if (val == 0)
		return -1;
	return __builtin_ctzll(val);
}
static inline _unused int leading_zeros(uint64_t val)
{
	if (val == 0)
		return -1;
	return __builtin_clzll(val);
}
static inline _unused size_t count_ones(uint64_t val)
{
	return __builtin_popcountll(val);
}
static inline _unused size_t align_down(size_t align, size_t val)
{
	assert(align > 0);
	assert(1 << trailing_zeros(align) == align);
	return val & ~(align - 1);
}
static inline _unused size_t align_up(size_t align, size_t val)
{
	return align_down(align, val + align - 1);
}
/// Pause CPU
static inline _unused void spin_wait()
{
#if defined(__x86_64__) || defined(_M_X64) || defined(i386) || \
	defined(__i386__) || defined(__i386) || defined(_M_IX86)
	asm("pause" ::);
#elif defined(__aarch64__) || defined(_M_ARM64)
	asm("isb" ::);
#else
#error Unknown architecture
#endif
}

// Error codes
enum {
	/// Success
	ERR_OK = 0,
	/// Not enough memory
	ERR_MEMORY = -1,
	/// Failed atomic operation, retry procedure
	ERR_RETRY = -2,
	/// Invalid address
	ERR_ADDRESS = -3,
	/// Allocator not initialized or initialization failed
	ERR_INITIALIZATION = -4,
	/// Corrupted allocator state
	ERR_CORRUPTION = -5,
};

/// Result type, to distinguish between normal integers
///
/// Errors are negative and the actual values are zero or positive.
typedef struct result {
	int64_t val;
} result_t;
/// Create a new result
static inline result_t _unused result(int64_t v)
{
	return (result_t){ v };
}
/// Check if the result is ok (no error)
static inline bool _unused result_ok(result_t r)
{
	return r.val >= 0;
}

/// Init modes
enum {
	/// Not persistent
	VOLATILE = 0,
	/// Persistent and try recovery
	RECOVER = 1,
	/// Overwrite the persistent memory
	OVERWRITE = 2,
};

#define tree_from_pfn(_N) ((_N) >> TREE_SHIFT)
#define pfn_from_tree(_N) ((_N) << TREE_SHIFT)

#define child_from_pfn(_N) ((_N) >> CHILD_SHIFT)
#define pfn_from_child(_N) ((_N) << CHILD_SHIFT)

#define row_from_pfn(_N) ((_N) >> ATOMIC_SHIFT)
#define pfn_from_row(_N) ((_N) << ATOMIC_SHIFT)

#define tree_from_row(_N) tree_from_pfn(pfn_from_row(_N))

// Maximum amount of retry if a atomic operation has failed
static const size_t MAX_ATOMIC_RETRY = 5;

static const int ATOM_LOAD_ORDER = memory_order_acquire;
static const int ATOM_UPDATE_ORDER = memory_order_acq_rel;
static const int ATOM_STORE_ORDER = memory_order_release;

/// Iterates over a Range between multiples of len starting at idx.
///
/// Starting at idx up to the next Multiple of len (exclusive). Then the next
/// step will be the highest multiple of len less than idx. (_base_idx)
/// Loop will end after len iterations.
/// code will be executed in each loop.
/// The current loop value can accessed by current_i
#define for_offsetted(idx, len)                                   \
	for (size_t _i = 0, _offset = (idx) % (len),              \
		    _base_idx = (idx)-_offset, current_i = (idx); \
	     _i < (len);                                          \
	     _i = _i + 1, current_i = _base_idx + ((_i + _offset) % (len)))

/// Checks if `obj` contains `expected` and writes `disired` to it if so.
#define atom_cmp_exchange(obj, expected, desired)                          \
	({                                                                 \
		debug("cmpxchg");                                          \
		atomic_compare_exchange_strong_explicit((obj), (expected), \
							(desired),         \
							ATOM_UPDATE_ORDER, \
							ATOM_LOAD_ORDER);  \
	})

/**
 * @brief wrapper for atomic load
 *
 */
#define atom_load(obj)                                      \
	({                                                  \
		debug("load");                              \
		atomic_load_explicit(obj, ATOM_LOAD_ORDER); \
	})
#define atom_store(obj, val)                                       \
	({                                                         \
		debug("store");                                    \
		atomic_store_explicit(obj, val, ATOM_STORE_ORDER); \
	})

#define atom_and(obj, mask)                                              \
	({                                                               \
		debug("and");                                            \
		atomic_fetch_and_explicit(obj, mask, ATOM_UPDATE_ORDER); \
	})

/// Atomic fetch-modify-update macro.
///
/// This macro loads the value at `atom_ptr`, stores its result in `old_val`
/// and then executes the `fn` function which is expected take a
/// customizable ctx parameter and a pointer to the loaded value,
/// which should be modified and is then stored atomically with CAS.
///
/// Returns if the update was successfull. Fails only if `fn` returns false.
///
/// Example:
/// ```
/// bool my_update(uint64_t *value, void *ctx) {
///     *value *= *value;
///     return true;
/// }
///
/// _Atomic uint64_t my_atomic;
/// uint64_t old;
/// if (!atom_update(&my_atomic, old, my_update, NULL)) {
/// 	assert(!"here this should never fail!");
/// }
/// printf("old value %lu\n", old);
/// ```
#define atom_update(atom_ptr, old_val, fn, ...)                              \
	({                                                                   \
		debug("update");                                             \
		bool _ret = false;                                           \
		(old_val) = atomic_load_explicit(atom_ptr, ATOM_LOAD_ORDER); \
		while (true) {                                               \
			typeof(old_val) value = (old_val);                   \
			if (!(fn)(&value, ##__VA_ARGS__))                    \
				break;                                       \
			if (atomic_compare_exchange_weak_explicit(           \
				    (atom_ptr), &(old_val), value,           \
				    ATOM_UPDATE_ORDER, ATOM_LOAD_ORDER)) {   \
				_ret = true;                                 \
				break;                                       \
			}                                                    \
		}                                                            \
		_ret;                                                        \
	})

/// Executes given function up to MAX_ATOMIC_RETRY times or until
/// it returns anything different than ERR_RETRY.
///
/// Returns the result of given function.
#define try_update(func)                                              \
	({                                                            \
		result_t _ret;                                        \
		for (size_t _i_ = 0; _i_ < MAX_ATOMIC_RETRY; ++_i_) { \
			_ret = func;                                  \
			if (_ret.val != ERR_RETRY)                    \
				break;                                \
		}                                                     \
		result_ok(_ret);                                      \
	})

// #define VERBOSE

#ifdef VERBOSE
#define info(str, ...)                                                \
	printf("\x1b[90m%s:%d: " str "\x1b[0m\n", __FILE__, __LINE__, \
	       ##__VA_ARGS__)
#else
#define info(str, ...)
#endif

#define warn(str, ...)                                                \
	printf("\x1b[93m%s:%d: " str "\x1b[0m\n", __FILE__, __LINE__, \
	       ##__VA_ARGS__)

// #define DEBUG

#ifdef DEBUG
#define debug(str, ...)                                               \
	printf("\x1b[90m%s:%d: " str "\x1b[0m\n", __FILE__, __LINE__, \
	       ##__VA_ARGS__)
#else
#define debug(str, ...)
#endif
