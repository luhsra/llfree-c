#pragma once

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

/// Result type, to distinguish between normal integers
///
/// Errors are negative and the actual values are zero or positive.
typedef struct result {
	int64_t val;
} result_t;

typedef struct llfree llfree_t;

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
result_t llfree_init(llfree_t *self, size_t cores, uint64_t offset, size_t len,
		  uint8_t init, uint8_t free_all);

/// Allocates a frame and returns its number, or a negative error code
result_t llfree_get(llfree_t *self, size_t core, size_t order);

/// Frees a frame, returning 0 on success or a negative error code
result_t llfree_put(llfree_t *self, size_t core, uint64_t frame, size_t order);

/// Frees a frame, returning 0 on success or a negative error code
result_t llfree_drain(llfree_t *self, size_t core);

/// Checks if a frame is allocated, returning 0 if not
bool llfree_is_free(llfree_t *self, uint64_t frame, size_t order);

/// Returns the total number of frames the allocator can allocate
uint64_t llfree_frames(llfree_t *self);

/// Returns number of currently free frames
uint64_t llfree_free_frames(llfree_t *self);

/// Destructs the allocator
void llfree_drop(llfree_t *self);

// == Debugging ==

/// Prints the allocators state for debugging with given Rust printer
void llfree_debug(llfree_t *self, void (*writer)(void *, char *), void *arg);

/// Prints detailed stats about the allocator state
void llfree_print(llfree_t *self);

/// Calls f for each Huge Frame. f will receive the context the current pfn
/// and the free counter as arguments
/// - used by some rust benchmarks like frag.rs
void llfree_for_each_huge(llfree_t *self, void *context,
		       void f(void *, uint64_t, uint64_t));

/// Allocate metadata function
extern void *llfree_ext_alloc(size_t align, size_t size);
/// Free metadata function
extern void llfree_ext_free(size_t align, size_t size, void *addr);
