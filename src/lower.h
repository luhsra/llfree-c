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
lower_result_t lower_init(lower_t *self, size_t frames, uint8_t init,
			  uint8_t *primary);

/// Size of the required metadata
size_t lower_metadata_size(size_t frames);
/// Returns the metadata
uint8_t *lower_metadata(const lower_t *self);

/// Allocates the given frame, returning its number or an error
lower_result_t lower_get(lower_t *self, size_t start_frame, ll_optional_t frame,
			 size_t order);

/// Deallocates the given frame
lower_result_t lower_put(lower_t *self, size_t frame, size_t order);

/// Counts free/huge/zeroed/reclaimed frames
ll_stats_t lower_stats(const lower_t *self);
/// Returns the stats for the frame (order == 0), huge frame (order == LLFREE_HUGE_ORDER),
/// or tree (order == LLFREE_TREE_ORDER)
ll_stats_t lower_stats_at(const lower_t *self, size_t frame, size_t order);

/// Print debug info
void lower_print(const lower_t *self);
