#pragma once

#include "bitfield.h"
#include "child.h"

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
uint8_t *lower_metadata(lower_t *self);

/// Allocates the given frame, returning its number or an error
llfree_result_t lower_get(lower_t *self, uint64_t start_frame, size_t order);

/// Deallocates the given frame
llfree_result_t lower_put(lower_t *self, uint64_t frame, size_t order);

/// Checks if the frame is free
bool lower_is_free(lower_t *self, uint64_t frame, size_t order);


/// Returns the number of huge frames
size_t lower_huge(lower_t *self);
/// Returns the number of free base frames
size_t lower_free_frames(lower_t *self);
/// Returns the number of free huge frames
size_t lower_free_huge(lower_t *self);

/// Returns the number of free pages in the huge page
size_t lower_free_at_huge(lower_t *self, uint64_t frame);
/// Returns the number of free pages in the huge page
size_t lower_free_at_tree(lower_t *self, uint64_t frame);

/// Search for a free and mapped huge page and change its status to inflated
llfree_result_t lower_unmap(lower_t *self, uint64_t start_frame, bool alloc);
/// Mark the inflated huge page as free, but keep it inflated
llfree_result_t lower_unmap_put(lower_t *self, uint64_t frame);
/// Mark the given huge page as entirely deflated
llfree_result_t lower_map(lower_t *self, uint64_t frame);
/// Return wether a frame is inflated
bool lower_is_unmapped(lower_t *self, uint64_t frame);

/// Print debug info
void lower_print(lower_t *self);
