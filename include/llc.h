#pragma once

#include <inttypes.h>
#include <stdbool.h>
#include <stdatomic.h>

/// Number of Bytes in cacheline
#define CACHE_SIZE 64

#define FRAME_BITS 12
/// Size of a base frame
#define FRAME_SIZE (1 << FRAME_BITS)

/// Order of a huge frame
#define HP_ORDER 9
/// Maximum order that can be allocated
#define MAX_ORDER (HP_ORDER + 1)

/// Num of bits of the larges atomic type of the architecture
#define ATOMIC_ORDER 6
#define ATOMIC_SIZE (1 << ATOMIC_ORDER)

/// Number of frames in a child
#define CHILD_ORDER HP_ORDER
#define CHILD_SIZE (1 << CHILD_ORDER)

/// Number of frames in a tree
#define TREE_CHILDREN_ORDER 5
#define TREE_CHILDREN (1 << TREE_CHILDREN_ORDER)
#define TREE_ORDER (HP_ORDER + TREE_CHILDREN_ORDER)
#define TREE_SIZE (1 << TREE_ORDER)

/// Result type, to distinguish between normal integers
///
/// Errors are negative and the actual values are zero or positive.
typedef struct result {
	int64_t val;
} result_t;

typedef struct llc llc_t;

/// Allocate and initialize the data structures of the allocator.
///
/// `offset` is the number of the first page to be managed and `len` determins
/// the size of the region in the number of pages.
///
/// The `init` parameter determins which memory is used:
/// - INIT_VOLATILE:  allocator uses volatile memory for its own data structures
/// - INIT_OVERWRITE: allocator uses parts of the persistent managed memory for its data structures
/// - INIT_RECOVER:   similar to INIT_OVERWRITE, but tries to recover from persistent memory.
///
/// `all_free` determins whether the region is initalized as entirely free
/// or entirely allocated.
result_t llc_init(llc_t *self, size_t cores, uint64_t offset, size_t len,
		  uint8_t init, uint8_t free_all);

/// Allocates a frame and returns its number, or a negative error code
result_t llc_get(llc_t *self, size_t core, size_t order);

/// Frees a frame, returning 0 on success or a negative error code
result_t llc_put(llc_t *self, size_t core, uint64_t frame, size_t order);

/// Frees a frame, returning 0 on success or a negative error code
result_t llc_drain(llc_t *self, size_t core);

/// Checks if a frame is allocated, returning 0 if not
bool llc_is_free(llc_t *self, uint64_t frame, size_t order);

/// Returns the total number of frames the allocator can allocate
uint64_t llc_frames(llc_t *self);

/// Returns number of currently free frames
uint64_t llc_free_frames(llc_t *self);

/// Destructs the allocator
void llc_drop(llc_t *self);

// == Debugging ==

/// Prints the allocators state for debugging with given Rust printer
void llc_debug(llc_t *self, void (*writer)(void *, char *), void *arg);

/// Prints detailed stats about the allocator state
void llc_print(llc_t *self);

/// Calls f for each Huge Frame. f will receive the context the current pfn
/// and the free counter as arguments
/// - used by some rust benchmarks like frag.rs
void llc_for_each_huge(llc_t *self, void *context,
		       void f(void *, uint64_t, uint64_t));

/// Allocate metadata function
extern void *llc_ext_alloc(size_t align, size_t size);
/// Free metadata function
extern void llc_ext_free(size_t align, size_t size, void *addr);

#if defined(KERNEL)

#include <linux/printk.h>
#include <linux/bug.h>

#define assert(con) BUG_ON(con)

#define pr_fmt(fmt) "llc: " fmt

#define warn(str, ...) pr_warn(str, ##__VA_ARGS__)

#ifdef VERBOSE
#define info(str, ...) pr_info(str, ##__VA_ARGS__)
#else
#define info(str, ...)
#endif

#ifdef DEBUG
#define debug(str, ...) pr_debug(str, ##__VA_ARGS__)
#else
#define debug(str, ...)
#endif

#else

#include <stdio.h>
#include <assert.h>

#define warn(str, ...)                                                \
	printf("\x1b[93m%s:%d: " str "\x1b[0m\n", __FILE__, __LINE__, \
	       ##__VA_ARGS__)

#ifdef VERBOSE
#define info(str, ...)                                                \
	printf("\x1b[90m%s:%d: " str "\x1b[0m\n", __FILE__, __LINE__, \
	       ##__VA_ARGS__)
#else
#define info(str, ...)
#endif

#ifdef DEBUG
#define debug(str, ...)                                               \
	printf("\x1b[90m%s:%d: " str "\x1b[0m\n", __FILE__, __LINE__, \
	       ##__VA_ARGS__)
#else
#define debug(str, ...)
#endif

#endif
