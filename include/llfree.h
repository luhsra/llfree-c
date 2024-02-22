#pragma once

#include "llfree_types.h"

/// Unused functions and variables
#define _unused __attribute__((unused))

#ifdef __clang__
#define _warn_unused __attribute__((warn_unused_result))
#else
#define _warn_unused
#endif

/// Result type, to distinguish between normal integers
///
/// Errors are negative and the actual values are zero or positive.
typedef struct _warn_unused llfree_result {
	int64_t val;
} llfree_result_t;

typedef struct llfree llfree_t;

/// Create a new result
static inline llfree_result_t _unused llfree_result(int64_t v)
{
	return (llfree_result_t){ v };
}
/// Check if the result is ok (no error)
static inline bool _unused llfree_ok(llfree_result_t r)
{
	return r.val >= 0;
}

/// Error codes
enum {
	/// Success
	LLFREE_ERR_OK = 0,
	/// Not enough memory
	LLFREE_ERR_MEMORY = -1,
	/// Failed atomic operation, retry procedure
	LLFREE_ERR_RETRY = -2,
	/// Invalid address
	LLFREE_ERR_ADDRESS = -3,
	/// Allocator not initialized or initialization failed
	LLFREE_ERR_INIT = -4,
};

/// Init modes
enum {
	/// Clear the allocator marking all frames as free
	LLFREE_INIT_FREE = 0,
	/// Clear the allocator marking all frames as allocated
	LLFREE_INIT_ALLOC = 1,
	/// Try recovering all frames from persistent memory
	LLFREE_INIT_RECOVER = 2,
	/// Try recovering all frames from persistent memory after a crash,
	/// correcting invalid counters
	LLFREE_INIT_RECOVER_CRASH = 3,
};

/// Allocate and initialize the data structures of the allocator.
///
/// `offset` is the number of the first page to be managed and `len` determins
/// the size of the region in the number of pages.
///
/// The `init` parameter is expected to be one of the `LLFREE_INIT_<..>` modes.
///
/// The `primary` and `secondary` buffer are used to store the allocator state
/// and must be at least as large as reported by llfree_metadata_size.
llfree_result_t llfree_init(llfree_t *self, size_t cores, size_t frames,
			    uint8_t init, uint8_t *primary, uint8_t *secondary);

/// Size of the required metadata
typedef struct llfree_meta_size {
	/// Size of the optionally persistent data.
	size_t primary;
	/// Size of the volatile data.
	size_t secondary;
} llfree_meta_size_t;

/// Returns the size of the metadata buffers required for initialization
llfree_meta_size_t llfree_metadata_size(size_t cores, size_t frames);

/// Size of the required metadata
typedef struct llfree_meta {
	/// Size of the optionally persistent data.
	uint8_t *primary;
	/// Size of the volatile data.
	uint8_t *secondary;
} llfree_meta_t;

/// Returns the metadata
llfree_meta_t llfree_metadata(llfree_t *self);

/// Allocates a frame and returns its number, or a negative error code
llfree_result_t llfree_get(llfree_t *self, size_t core, size_t order);

/// Frees a frame, returning 0 on success or a negative error code
llfree_result_t llfree_put(llfree_t *self, size_t core, uint64_t frame,
			   size_t order);

/// Unreserves all cpu-local reservations
llfree_result_t llfree_drain(llfree_t *self, size_t core);

/// Checks if a frame is allocated, returning 0 if not
bool llfree_is_free(llfree_t *self, uint64_t frame, size_t order);

/// Returns the number of cores this allocator was initialized with
size_t llfree_cores(llfree_t *self);

/// Returns the total number of frames the allocator can allocate
size_t llfree_frames(llfree_t *self);

/// Returns number of currently free frames
size_t llfree_free_frames(llfree_t *self);
/// Returns number of currently free frames
size_t llfree_free_huge(llfree_t *self);

// == Debugging ==

/// Prints the allocators state for debugging with given Rust printer
void llfree_print_debug(llfree_t *self, void (*writer)(void *, char *),
			void *arg);

#ifdef STD
/// Prints detailed stats about the allocator state
void llfree_print(llfree_t *self);
#endif

/// Calls f for each Huge Frame. f will receive the context the current pfn
/// and the free counter as arguments
/// If f returns false, the iteration stops and this function returns false.
///
/// - used by some rust benchmarks like frag.rs
bool llfree_for_each_huge(llfree_t *self, void *context,
			  bool f(void *, uint64_t, size_t));
