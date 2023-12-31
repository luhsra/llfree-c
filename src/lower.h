#pragma once

#include "bitfield.h"
#include "child.h"

typedef struct lower {
	/// number of managed frames
	size_t frames;
	/// array length for fields and childs
	size_t childs_len;
	/// bitfields storing the allocation states of the pages
	bitfield_t *fields;
	/// index per bitfield
	_Atomic(child_t) *children;
} lower_t;

/// Allocate and initialize the data structures of the lower allocator.
llfree_result_t lower_init(lower_t *self, size_t frames, uint8_t init, uint8_t *primary);

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

/// Returns the number of free base frames
size_t lower_free_frames(lower_t *self);

/// Returns the number of free huge frames
size_t lower_free_huge(lower_t *self);

#ifdef STD
/// Print llfree_debug llfree_info
void lower_print(lower_t *self);
#endif

/// Calls f for each child. f will receive the context the current pfn and the free counter as arguments
// used by frag.rs benchmark
void lower_for_each_child(const lower_t *self, void *context,
			  void f(void *, uint64_t, size_t));
