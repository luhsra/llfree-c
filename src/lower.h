#pragma once

#include "bitfield.h"
#include "child.h"

typedef struct lower {
	/// first pfn of managed space
	uint64_t offset;
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
///
/// The `init` parameter determins which memory is used:
/// - LLFREE_INIT_VOLATILE:  allocator uses volatile memory for its own data structures
/// - LLFREE_INIT_OVERWRITE: allocator uses parts of the persistent managed memory for its data structures
/// - LLFREE_INIT_RECOVER:   similar to LLFREE_INIT_OVERWRITE, but tries to recover from persistent memory.
void lower_init(lower_t *self, uint64_t offset, size_t len, uint8_t init);

/// Resets the contents of the lower allocator to everything allocated or free based on `free_all`.
///
/// Note: this is not thread safe.
void lower_clear(lower_t *self, bool free_all);

/// Recovers the state from persistent memory
///
/// Checks and possibly corrects the free counter in childs
llfree_result_t lower_recover(lower_t *self);

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

/// Destructs the allocator, freeing its metadata
void lower_drop(lower_t *self);

/// Calls f for each child. f will receive the context the current pfn and the free counter as arguments
// used by frag.rs benchmark
void lower_for_each_child(const lower_t *self, void *context,
			  void f(void *, uint64_t, size_t));
