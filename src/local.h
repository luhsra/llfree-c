#pragma once

#include "utils.h"

#define LAST_FREES 4U

/// CPU-local data of the currently reserved tree
typedef struct reserved {
	/// Number of free frames in the tree
	uint16_t free : 15;
	/// Bitfield row index of reserved tree,
	/// used for identifying the reserved tree and as starting point
	/// for the next allocation
	uint64_t start_row : 47;
	/// true if there is a reserved tree
	bool present : 1;
	/// Used to synchronize concurrent reservations
	bool lock : 1;
} reserved_t;

/// Stores information about the last frees
typedef struct last_free {
	/// Counts concurrent frees in same tree
	uint16_t counter : 4;
	/// Index of the last tree where a frame was freed
	uint64_t tree_idx : 60;
} last_free_t;

/// This represents the local CPU data
typedef struct __attribute__((aligned(LLFREE_CACHE_SIZE))) local {
	_Atomic(reserved_t) reserved;
	_Atomic(last_free_t) last_free;
} local_t;

/// Initialize the per-cpu data
void local_init(local_t *self);

/// Changes the preferred tree (and free counter) to a new one
bool reserved_swap(reserved_t *self, reserved_t new, bool expect_lock);

/// Increases the free counter if tree of frame matches the reserved tree
bool reserved_inc(reserved_t *self, size_t tree_idx, size_t free);

/// Decrements the free counter
bool reserved_dec(reserved_t *self, size_t free);

/// Decrements the free counter or sets the lock otherwise
bool reserved_dec_or_lock(reserved_t *self, size_t free, bool *locked);

/// Updates the start index to speedup the next search for a free frame
bool reserved_set_start(reserved_t *self, size_t row_idx);

/// Set the reserving flag, returning false if it already has the specified value
bool reserved_set_lock(reserved_t *self, bool lock);

/// Updates the last-frees heuristic, returning true if the corresponding
/// tree should be reserved
bool last_free_inc(last_free_t *self, uint64_t tree_idx);
