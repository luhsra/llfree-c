#pragma once

#include "bitfield.h"
#include "child.h"
#include "utils.h"

#define CHILDS_PER_TREE 32
#define PAGESIZE (1 << 12)

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
	_Atomic(child_t) *childs;
} lower_t;

/// Allocate and initialize the data structures of the lower allocator.
///
/// The `init` parameter determins which memory is used:
/// - VOLATILE:  allocator uses volatile memory for its own data structures
/// - OVERWRITE: allocator uses parts of the persistent managed memory for its data structures
/// - RECOVER:   similar to OVERWRITE, but tries to recover from persistent memory.
void lower_init(lower_t *const self, uint64_t offset, size_t len, uint8_t init);

/// Resets the contents of the lower allocator to everything allocated or free based on `free_all`.
///
/// Note: this is not thread safe.
void lower_clear(lower_t *self, bool free_all);

/// Recovers the state from persistent memory
///
/// Checks and possibly corrects the free counter in childs
result_t lower_recover(lower_t *self);

/// Allocates the given frame, returning its number or an error
result_t lower_get(lower_t *self, uint64_t frame, size_t order);

/// Deallocates the given frame
result_t lower_put(lower_t *self, uint64_t frame, size_t order);

/// Checks if the frame is free
bool lower_is_free(lower_t *self, uint64_t frame, size_t order);

/// Returns the number of free base frames
size_t lower_free_frames(lower_t *self);

/// Returns the number of free huge frames
size_t lower_free_huge(lower_t *self);

/// Print debug info
void lower_print(lower_t *self);

/// Destructs the allocator, freeing its metadata
void lower_drop(lower_t *self);

/// Calls f for each child. f will receive the context the current pfn and the free counter as arguments
// used by frag.rs benchmark
void lower_for_each_child(const lower_t *self, void *context,
			  void f(void *, uint64_t, uint64_t));
