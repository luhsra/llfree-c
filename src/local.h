#pragma once

#include "utils.h"

/// Reservation heuristic: number of frees in the same tree before reserving it
#define LAST_FREES 4U
/// Reservation heuristic: search globally every n reservations
#define SKIP_NEAR_FREQ 4U

/// CPU-local data of the currently reserved tree
typedef struct reserved {
	/// Number of free frames in the tree
	uint16_t free : 15;
	/// Bitfield row index of reserved tree,
	/// used for identifying the reserved tree and as starting point
	/// for the next allocation
	uint64_t start_row : 48;
	/// true if there is a reserved tree
	bool present : 1;
} reserved_t;

/// This represents the local CPU data
typedef struct __attribute__((aligned(LLFREE_CACHE_SIZE))) local {
	/// Protect the local data from concurrent access (in case of shared local data or parallel drain)
	_Atomic(bool) lock;
	/// Counts concurrent frees in same tree
	uint8_t last_frees;
	/// Counter that triggers full scans (fragmentation heuristic)
	uint8_t skip_near_counter;
	/// Reserved tree
	reserved_t reserved;
	/// Index of the last tree where a frame was freed
	uint64_t last_idx;
} local_t;

/// Initialize the per-cpu data
void ll_local_init(local_t *self);

/// Locks the reserved tree
void ll_local_lock(local_t *self);
bool ll_local_try_lock(local_t *self);

/// Unlocks the reserved tree
void ll_local_unlock(local_t *self);

/// Updates the last-frees heuristic, returning true if the corresponding
/// tree should be reserved
bool ll_local_free_inc(local_t *self, uint64_t tree_idx);
