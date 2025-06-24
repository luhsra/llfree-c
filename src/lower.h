#pragma once

#include "bitfield.h"
#include "child.h"
#include "llfree.h"

typedef struct children {
	_Alignas(LLFREE_CACHE_SIZE) _Atomic(child_t)
		entries[LLFREE_TREE_CHILDREN];
} children_t;

typedef struct lower {
	/// number of managed frames
	size_t frames;
	/// bitfields storing the allocation states of the pages
	bitfield_t *fields;
	/// index per bitfield
	children_t *children;
} lower_t;

/// Allocate and initialize the data structures of the lower allocator.
llfree_result_t lower_init(lower_t *self, size_t frames, uint8_t init,
			   uint8_t *primary);

/// Size of the required metadata
size_t lower_metadata_size(size_t frames);
/// Returns the metadata
uint8_t *lower_metadata(const lower_t *self);

/// Allocates the given frame, returning its number or an error
llfree_result_t lower_get(lower_t *self, uint64_t start_frame, llflags_t flags);
/// Try allocating the given frame
llfree_result_t lower_get_at(lower_t *self, uint64_t frame, llflags_t flags);

/// Deallocates the given frame
llfree_result_t lower_put(lower_t *self, uint64_t frame, llflags_t flags);

/// Counts free/huge/zeroed/reclaimed frames
ll_stats_t lower_stats(const lower_t *self);
/// Returns the stats for the frame (order == 0), huge frame (order == LLFREE_HUGE_ORDER),
/// or tree (order == LLFREE_TREE_ORDER)
ll_stats_t lower_stats_at(const lower_t *self, uint64_t frame, size_t order);

/// Search for a free and not reclaimed huge page and mark it reclaimed (and optionally allocated)
llfree_result_t lower_reclaim(lower_t *self, uint64_t start_frame, bool hard,
			      bool zeroed);
/// Mark the reclaimed huge page as free, but keep it reclaimed
llfree_result_t lower_return(lower_t *self, uint64_t frame, bool install);
/// Clear the reclaimed state of the given huge page
llfree_result_t lower_install(lower_t *self, uint64_t frame);
/// Return wether a frame is reclaimed
bool lower_is_reclaimed(const lower_t *self, uint64_t frame);

/// Print debug info
void lower_print(const lower_t *self);
