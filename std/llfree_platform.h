#pragma once

#include "llfree_types.h"
#include <stdio.h>
#include <assert.h>
#include <stdatomic.h>

#define ll_align(align) __attribute__((aligned(align)))
#define unlikely(x) __builtin_expect(!!(x), 0)

#define PRIuS "zu"
#define PRIxS "zx"

/// Number of Bytes in cacheline
#define LLFREE_CACHE_SIZE 64u

#define LLFREE_FRAME_BITS 12u
/// Size of a base frame
#define LLFREE_FRAME_SIZE (1u << LLFREE_FRAME_BITS)

/// Order of a huge frame
#define LLFREE_HUGE_ORDER 9u
/// Maximum order that can be allocated
#define LLFREE_MAX_ORDER (LLFREE_HUGE_ORDER + 1u)

/// Num of bits of the larges atomic type of the architecture
#define LLFREE_ATOMIC_ORDER 6u
#define LLFREE_ATOMIC_SIZE (1u << LLFREE_ATOMIC_ORDER)

/// Number of frames in a child
#define LLFREE_CHILD_ORDER LLFREE_HUGE_ORDER
#define LLFREE_CHILD_SIZE (1u << LLFREE_CHILD_ORDER)

/// Number of frames in a tree
#define LLFREE_TREE_CHILDREN_ORDER 3u
#define LLFREE_TREE_CHILDREN (1u << LLFREE_TREE_CHILDREN_ORDER)
#define LLFREE_TREE_ORDER (LLFREE_HUGE_ORDER + LLFREE_TREE_CHILDREN_ORDER)
#define LLFREE_TREE_SIZE (1u << LLFREE_TREE_ORDER)

/// Enable reserve on free heuristic
#define LLFREE_ENABLE_FREE_RESERVE false
/// Allocate first from already install huge frames, before falling back to evicted ones
#define LLFREE_PREFER_INSTALLED false

/// Minimal alignment the llfree requires for its memory range
#define LLFREE_ALIGN (1u << LLFREE_MAX_ORDER << LLFREE_FRAME_BITS)

#define llfree_warn(str, ...)                                         \
	printf("\x1b[93m%s:%d: " str "\x1b[0m\n", __FILE__, __LINE__, \
	       ##__VA_ARGS__)

#define llfree_info_start() printf("\x1b[90m%s:%d: ", __FILE__, __LINE__)
#define llfree_info_cont(str, ...) printf(str, ##__VA_ARGS__)
#define llfree_info_end() printf("\x1b[0m\n")
#define llfree_info(str, ...)                                         \
	printf("\x1b[90m%s:%d: " str "\x1b[0m\n", __FILE__, __LINE__, \
	       ##__VA_ARGS__)

#ifdef DEBUG
#define llfree_debug(str, ...)                                        \
	printf("\x1b[90m%s:%d: " str "\x1b[0m\n", __FILE__, __LINE__, \
	       ##__VA_ARGS__)
#else
#define llfree_debug(str, ...)
#endif

static const int ATOM_LOAD_ORDER = memory_order_acquire;
static const int ATOM_UPDATE_ORDER = memory_order_acq_rel;
static const int ATOM_STORE_ORDER = memory_order_release;

/// Checks if `obj` contains `expected` and writes `disired` to it if so.
#define atom_cmp_exchange(obj, expected, desired)                          \
	({                                                                 \
		llfree_debug("cmpxchg");                                   \
		atomic_compare_exchange_strong_explicit((obj), (expected), \
							(desired),         \
							ATOM_UPDATE_ORDER, \
							ATOM_LOAD_ORDER);  \
	})
#define atom_cmp_exchange_weak(obj, expected, desired)                   \
	({                                                               \
		llfree_debug("cmpxchg");                                 \
		atomic_compare_exchange_weak_explicit((obj), (expected), \
						      (desired),         \
						      ATOM_UPDATE_ORDER, \
						      ATOM_LOAD_ORDER);  \
	})

#define atom_swap(obj, desired)                                            \
	({                                                                 \
		llfree_debug("swap");                                      \
		atomic_exchange_explicit(obj, desired, ATOM_UPDATE_ORDER); \
	})

#define atom_load(obj)                                      \
	({                                                  \
		llfree_debug("load");                       \
		atomic_load_explicit(obj, ATOM_LOAD_ORDER); \
	})
#define atom_store(obj, val)                                       \
	({                                                         \
		llfree_debug("store");                             \
		atomic_store_explicit(obj, val, ATOM_STORE_ORDER); \
	})

/// Atomic fetch-modify-update macro.
///
/// This macro loads the value at `atom_ptr`, stores its llfree_result in `old_val`
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
/// printf("old value %u\n", old);
/// ```
#define atom_update(atom_ptr, old_val, fn, ...)                              \
	({                                                                   \
		/* NOLINTBEGIN */                                            \
		llfree_debug("update");                                      \
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

#ifndef STD
#define STD 1
#endif
