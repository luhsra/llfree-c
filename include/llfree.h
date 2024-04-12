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
	/// Assume the allocator is already initialized
	LLFREE_INIT_NONE = 4,
	/// The number of initialization modes
	LLFREE_INIT_MAX = 5,
};

/// Allocation flags
typedef struct llflags {
	uint8_t order : 8;
	/// Is this alloation movable: LLFree tries to separete movable and immovable allocations.
	bool movable : 1;
	/// Search for huge pages that are not reported for ballooning
	bool get_unreported : 1;
	/// Mark this huge page as reported for ballooning
	bool set_reported : 1;
	/// Skip cpu-local reservations and ignore tree kinds (like movable).
	bool global : 1;
	// ... Reserved for future use
} llflags_t;

static inline llflags_t llflags(size_t order)
{
	return (llflags_t){ .order = (uint8_t)order,
			    .movable = false,
			    .get_unreported = false,
			    .set_reported = false,
			    .global = false };
}

/// Size of the required metadata
typedef struct llfree_meta_size {
	/// Volatile data.
	size_t llfree;
	/// CPU-local data.
	size_t local;
	/// Tree array.
	size_t trees;
	/// Lower children and bitfields (optionally persistent).
	size_t lower;
} llfree_meta_size_t;

/// Returns the size of the metadata buffers required for initialization
llfree_meta_size_t llfree_metadata_size(size_t cores, size_t frames);

/// Size of the required metadata
typedef struct llfree_meta {
	/// CPU-local data.
	uint8_t *local;
	/// Tree array.
	uint8_t *trees;
	/// Lower children and bitfields (optionally persistent).
	uint8_t *lower;
} llfree_meta_t;

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
			    uint8_t init, llfree_meta_t meta);

/// Returns the metadata
llfree_meta_t llfree_metadata(llfree_t *self);

/// Allocates a frame and returns its number, or a negative error code
llfree_result_t llfree_get(llfree_t *self, size_t core, llflags_t flags);
/// Frees a frame, returning 0 on success or a negative error code
llfree_result_t llfree_put(llfree_t *self, size_t core, uint64_t frame,
			   llflags_t flags);

/// Unreserves all cpu-local reservations
llfree_result_t llfree_drain(llfree_t *self, size_t core);

/// Returns the number of cores this allocator was initialized with
size_t llfree_cores(llfree_t *self);

/// Returns the total number of frames the allocator can allocate
size_t llfree_frames(llfree_t *self);

/// Returns the total number of huge frames the allocator can allocate
size_t llfree_huge(llfree_t *self);

/// Checks if a frame is allocated, returning 0 if not
bool llfree_is_free(llfree_t *self, uint64_t frame, size_t order);
/// Returns the number of frames in the given chunk.
/// Note: This is only implemented for 0, HUGE_ORDER and TREE_ORDER.
size_t llfree_free_at(llfree_t *self, uint64_t frame, size_t order);

/// Returns number of currently free frames
size_t llfree_free_frames(llfree_t *self);
/// Returns number of currently free frames
size_t llfree_free_huge(llfree_t *self);

// == Debugging ==

/// Prints the allocators state for debugging with given Rust printer
void llfree_print_debug(llfree_t *self, void (*writer)(void *, char *),
			void *arg);

/// Prints detailed stats about the allocator state
void llfree_print(llfree_t *self);
/// Validate the internal data structures
void llfree_validate(llfree_t *self);
