#pragma once

#include <stdbool.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <stdalign.h>

#include "llc.h"

/// Unused functions and variables
#define _unused __attribute__((unused))

// conversion functions
#define tree_from_pfn(_N) ((_N) >> TREE_ORDER)
#define pfn_from_tree(_N) ((_N) << TREE_ORDER)

#define child_from_pfn(_N) ((_N) >> CHILD_ORDER)
#define pfn_from_child(_N) ((_N) << CHILD_ORDER)

#define row_from_pfn(_N) ((_N) >> ATOMIC_ORDER)
#define pfn_from_row(_N) ((_N) << ATOMIC_ORDER)

#define tree_from_row(_N) tree_from_pfn(pfn_from_row(_N))
#define row_from_tree(_N) row_from_pfn(pfn_from_tree(_N))

/// Minimal size the LLFree can manage
#define MIN_PAGES (1ul << MAX_ORDER)
/// 64 Bit Addresses - 12 Bit needed for offset inside the Page
#define MAX_PAGES (1ul << (64 - FRAME_BITS))
/// Number of retries
#define RETRIES 4

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))

/// Devides a by b, rounding up the result
static inline _unused size_t div_ceil(uint64_t a, size_t b)
{
	return (a + b - 1) / b;
}
/// Count the number of zero bits beginning at the lowest significant bit
static inline _unused int trailing_zeros(uint64_t val)
{
	if (val == 0)
		return -1;
	return __builtin_ctzll(val);
}
/// Count the number of zero bits beginning at the most significant bit
static inline _unused int leading_zeros(uint64_t val)
{
	if (val == 0)
		return -1;
	return __builtin_clzll(val);
}
/// Count the total number of ones
static inline _unused size_t count_ones(uint64_t val)
{
	return __builtin_popcountll(val);
}
/// Returns the largest multiple of align, less or equal to val
static inline _unused size_t align_down(size_t val, size_t align)
{
	assert(align > 0 && (1 << trailing_zeros(align) == align));
	return val & ~(align - 1);
}
/// Returns the smallest multiple of align, greater or equal to val
static inline _unused size_t align_up(size_t val, size_t align)
{
	return align_down(val + align - 1, align);
}
/// Pause CPU for polling
static inline _unused void spin_wait(void)
{
#if defined(__x86_64__) || defined(_M_X64) || defined(i386) || \
	defined(__i386__) || defined(__i386) || defined(_M_IX86)
	__asm("pause" ::);
#elif defined(__aarch64__) || defined(_M_ARM64)
	__asm("isb" ::);
#else
#error Unknown architecture
#endif
}

/// Error codes
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
	INIT_VOLATILE = 0,
	/// Persistent and try recovery
	INIT_RECOVER = 1,
	/// Overwrite the persistent memory
	INIT_OVERWRITE = 2,
};

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
/// and then executes the `fn` function with a pointer to the loaded value,
/// which should be modified and is then stored atomically with CAS.
/// The function `fn` can take any number of extra parameters,
/// that are passed directly into it.
///
/// Returns if the update was successfull.
/// Fails only if `fn` returns false.
///
/// Example:
/// ```
/// bool my_update(uint64_t *value, bool argument1, int argument 2) {
/// 	if (argument1) {
///     	*value *= *value;
///		return true;
///	}
///     return false;
/// }
///
/// _Atomic uint64_t my_atomic;
/// uint64_t old;
/// if (!atom_update(&my_atomic, old, my_update, false, 42)) {
/// 	assert(!"our my_update function returned false, cancelling the update");
/// }
/// printf("old value %ju\n", old);
/// ```
#define atom_update(atom_ptr, old_val, fn, ...)                              \
	({                                                                   \
		/* NOLINTBEGIN */                                            \
		debug("update");                                             \
		bool _ret = false;                                           \
		(old_val) = atomic_load_explicit(atom_ptr, ATOM_LOAD_ORDER); \
		while (true) {                                               \
			__typeof(old_val) value = (old_val);                 \
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
		/*NOLINTEND*/                                                \
	})
