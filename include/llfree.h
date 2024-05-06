#pragma once

#include "llfree_types.h"

/// Unused functions and variables
#define _unused __attribute__((unused))

#ifdef __clang__
#define _warn_unused __attribute__((warn_unused_result))
#else
#define _warn_unused
#endif

/// Success
#define LLFREE_ERR_OK ((uint8_t)0)
/// Not enough memory
#define LLFREE_ERR_MEMORY ((uint8_t)1)
/// Failed atomic operation, retry procedure
#define LLFREE_ERR_RETRY ((uint8_t)2)
/// Invalid address
#define LLFREE_ERR_ADDRESS ((uint8_t)3)
/// Allocator not initialized or initialization failed
#define LLFREE_ERR_INIT ((uint8_t)4)

/// Result type, to distinguish between normal integers
///
/// Errors are negative and the actual values are zero or positive.
typedef struct _warn_unused llfree_result {
	uint64_t frame : 55;
	bool unmapped : 1;
	uint8_t error : 8;
} llfree_result_t;

typedef struct llfree llfree_t;

/// Create a new result
static inline llfree_result_t _unused llfree_ok(uint64_t frame, bool unmapped)
{
	return (llfree_result_t){ .frame = frame,
				  .unmapped = unmapped,
				  .error = LLFREE_ERR_OK };
}
static inline llfree_result_t _unused llfree_err(uint8_t err)
{
	return (llfree_result_t){ .frame = 0, .unmapped = false, .error = err };
}

/// Check if the result is ok (no error)
static inline bool _unused llfree_is_ok(llfree_result_t r)
{
	return r.error == LLFREE_ERR_OK;
}

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
	/// Allocation size: n-th power of two.
	uint8_t order : 8;
	/// Is this alloation movable: LLFree tries to separete movable and immovable allocations.
	bool movable : 1;
	// ... Reserved for future use
} llflags_t;

static inline llflags_t _unused llflags(size_t order)
{
	return (llflags_t){ .order = (uint8_t)order, .movable = false };
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

// == Ballooning ==

/// Search for a free and mapped, huge page and mark it unmapped (and optionally allocated)
llfree_result_t llfree_unmap(llfree_t *self, size_t core, bool alloc);
/// Mark the unmapped huge page as free, but keep it unmapped
llfree_result_t llfree_unmap_put(llfree_t *self, uint64_t frame);
/// Mark the given huge page as mapped
llfree_result_t llfree_map(llfree_t *self, uint64_t frame);
/// Return wether a frame is unmapped
bool llfree_is_unmapped(llfree_t *self, uint64_t frame);

// == Debugging ==

/// Prints the allocators state for debugging with given Rust printer
void llfree_print_debug(llfree_t *self, void (*writer)(void *, char *),
			void *arg);

/// Prints detailed stats about the allocator state
void llfree_print(llfree_t *self);
/// Validate the internal data structures
void llfree_validate(llfree_t *self);
