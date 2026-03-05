#pragma once

#include "llfree_types.h"

/// Unused functions and variables
#define ll_unused __attribute__((unused))

#ifdef __clang__
#define ll_warn_unused __attribute__((warn_unused_result))
#else
#define ll_warn_unused
#endif

/// Optional size_t type
typedef struct ll_optional {
	bool present : 1;
	size_t value : (sizeof(size_t) * 8) - 1;
} ll_optional_t;
static inline ll_unused ll_optional_t ll_some(size_t value)
{
	return (ll_optional_t){ .present = true, .value = value };
}
static inline ll_unused ll_optional_t ll_none(void)
{
	return (ll_optional_t){ .present = false, .value = 0 };
}

#define LLKIND_BITS 3

/// Opaque kind of a tree
typedef struct llkind {
	uint8_t id : LLKIND_BITS;
} llkind_t;

/// Create a new kind
llkind_t llkind(uint8_t id);

/// Kind id used for huge frames
#define LLKIND_HUGE llkind((1 << LLKIND_BITS) - 2)
/// Maximum kind id (e.g., usable for zeroed or long-living huge pages)
#define LLKIND_ZERO llkind((1 << LLKIND_BITS) - 1)

/// Specifies how many reservations of each kind the allocator should maintain
typedef struct llkind_desc {
	llkind_t kind;
	uint8_t count;
} llkind_desc_t;

/// Create a new kind descriptor
llkind_desc_t llkind_desc(llkind_t kind, uint8_t count);
static inline ll_unused llkind_desc_t llkind_desc_zero(void)
{
	return (llkind_desc_t){ .kind = llkind(0), .count = 0 };
}

enum : uint8_t {
	/// Success
	LLFREE_ERR_OK = 0,
	/// Not enough memory
	LLFREE_ERR_MEMORY = 1,
	/// Failed atomic operation, retry procedure
	LLFREE_ERR_RETRY = 2,
	/// Invalid address
	LLFREE_ERR_ADDRESS = 3,
	/// Allocator not initialized or initialization failed
	LLFREE_ERR_INIT = 4,
};

/// Result type, to distinguish between normal integers
typedef struct ll_warn_unused llfree_result {
	/// Usually only valid if error == LLFREE_ERR_OK
	uint64_t frame : 58;
	/// If the frame was reclaimed, e.g. by ballooning
	uint8_t kind : LLKIND_BITS;
	/// Error code, 0 if no error
	uint8_t error : 3;
} llfree_result_t;

_Static_assert(sizeof(llfree_result_t) == sizeof(uint64_t),
	       "result size mismatch");

typedef struct llfree llfree_t;

/// Create a new result
static inline llfree_result_t ll_unused llfree_ok(uint64_t frame, llkind_t kind)
{
	return (llfree_result_t){ .frame = frame,
				  .kind = kind.id,
				  .error = LLFREE_ERR_OK };
}
static inline llfree_result_t ll_unused llfree_err(uint8_t err)
{
	return (llfree_result_t){ .frame = 0, .kind = 0, .error = err };
}

/// Check if the result is ok (no error)
static inline bool ll_unused llfree_is_ok(llfree_result_t r)
{
	return r.error == LLFREE_ERR_OK;
}

/// Init modes
enum : uint8_t {
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
	uint8_t order : 4;
	/// Local tree index (local trees are configured during initialization)
	///
	/// The policy for mapping locals to cores has to be provided by the user,
	/// but a common approach is to have one local tree per core.
	uint16_t local : 12;
} llflags_t;

/// Create new flags
llflags_t llflags(size_t order, size_t local);

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
llfree_meta_size_t llfree_metadata_size(const llkind_desc_t *kinds,
					size_t frames);

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
/// The zero-terminated `kinds` array specifies how many reservations of each
/// kind the allocator should maintain.
///
/// The `init` parameter is expected to be one of the `LLFREE_INIT_<..>` modes.
///
/// The `meta` buffers are used to store the allocator state
/// and must be at least as large as reported by `llfree_metadata_size`.
llfree_result_t llfree_init(llfree_t *self, const llkind_desc_t *kinds,
			    size_t frames, uint8_t init, llfree_meta_t meta);

/// Returns the metadata
llfree_meta_t llfree_metadata(const llfree_t *self);

/// Allocates a frame, optionally at a specific frame index.
llfree_result_t llfree_get(llfree_t *self, ll_optional_t frame,
			   llflags_t flags);
/// Frees this frame
llfree_result_t llfree_put(llfree_t *self, uint64_t frame, llflags_t flags);

/// Unreserves all cpu-local reservations
llfree_result_t llfree_drain(llfree_t *self, size_t local);

/// Returns the total number of frames the allocator can allocate
size_t llfree_frames(const llfree_t *self);

/// LLFree statistics
typedef struct ll_stats {
	size_t frames;
	size_t huge;
	size_t free_frames;
	size_t free_huge;
	size_t zeroed_huge;
	size_t reclaimed_huge;
} ll_stats_t;

/// Returns number of currently free/huge/zeroed frames.
/// Does not include reclaimed frames and huge/zeroed can be inaccurate.
ll_stats_t llfree_stats(const llfree_t *self);
/// Counts free/huge/zeroed/reclaimed frames.
ll_stats_t llfree_full_stats(const llfree_t *self);
/// Returns the stats for the frame (order == 0), huge frame (order == LLFREE_HUGE_ORDER),
/// or tree (order == LLFREE_TREE_ORDER).
/// Might not include reclaimed frames and huge/zeroed can be inaccurate.
ll_stats_t llfree_stats_at(const llfree_t *self, uint64_t frame, size_t order);
/// Returns the stats for the frame (order == 0), huge frame (order == LLFREE_HUGE_ORDER),
/// or tree (order == LLFREE_TREE_ORDER).
ll_stats_t llfree_full_stats_at(const llfree_t *self, uint64_t frame,
				size_t order);

// == Ballooning ==

#if 0
/// Search for a free and not reclaimed huge page and mark it reclaimed (and optionally allocated)
llfree_result_t llfree_reclaim(llfree_t *self, size_t local, bool alloc,
			       bool not_reclaimed, bool not_zeroed);
/// Mark the reclaimed huge page as free, but keep it reclaimed
llfree_result_t llfree_return(llfree_t *self, uint64_t frame, bool install);
/// Clear the reclaimed state of the given huge page
llfree_result_t llfree_install(llfree_t *self, uint64_t frame);
#endif

// == Debugging ==

/// Prints the allocators state for debugging with given Rust printer
void llfree_print_debug(const llfree_t *self,
			void (*writer)(void *, const char *), void *arg);

/// Prints detailed stats about the allocator state
void llfree_print(const llfree_t *self);
/// Validate the internal data structures
void llfree_validate(const llfree_t *self);
