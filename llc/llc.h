#pragma once

#include "lower.h"
#include "local.h"
#include "tree.h"
#include "utils.h"

typedef struct llc {
	struct meta *meta;
	lower_t lower;
	struct local *local;
	size_t cores; // array_size of local
	_Atomic(tree_t) *trees;
	size_t trees_len;
} llc_t;

/// Allocate and initialize the data structures of the allocator.
///
/// `offset` is the number of the first page to be managed and `len` determins
/// the size of the region in the number of pages.
///
/// The `init` parameter determins which memory is used:
/// - VOLATILE:  allocator uses volatile memory for its own data structures
/// - OVERWRITE: allocator uses parts of the persistent managed memory for its data structures
/// - RECOVER:   similar to OVERWRITE, but tries to recover from persistent memory.
///
/// `all_free` determins whether the region is initalized as entirely free
/// or entirely allocated.
result_t llc_init(llc_t *self, size_t cores, uint64_t offset, size_t len,
		  uint8_t init, uint8_t all_free);

/// Allocates a frame and returns its number, or a negative error code
result_t llc_get(llc_t *self, size_t core, size_t order);

/// Frees a frame, returning 0 on success or a negative error code
result_t llc_put(llc_t *self, size_t core, uint64_t frame, size_t order);

/// Checks if a frame is allocated, returning 0 if not
uint8_t llc_is_free(llc_t *self, uint64_t pfn, size_t order);

/// Returns the total number of frames the allocator can allocate
uint64_t llc_frames(llc_t *self);

/// Returns number of currently free frames
uint64_t llc_free_frames(llc_t *self);

/// Destructs the allocator
void llc_drop(llc_t *self);

/// Prints the allocators state for debugging with given Rust printer
void llc_debug(llc_t *self, void (*writer)(void *, char *), void *arg);

/// Prints detailed stats about the allocator state
void llc_print(llc_t *self);

/// Calls f for each Huge Frame. f will receive the context the current pfn and the free counter as arguments - used by some rust benchmarks like frag.rs
void llc_for_each_huge(llc_t *self, void *context,
		       void f(void *, uint64_t, uint64_t));

/// Allocate metadata function
extern void *llc_ext_alloc(size_t align, size_t size);
/// Free metadata function
extern void llc_ext_free(size_t align, size_t size, void *addr);
