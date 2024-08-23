#pragma once

#include "utils.h"
#include "tree.h"

/// Reservation heuristic: number of frees in the same tree before reserving it
#define LAST_FREES 4U
/// Reservation heuristic: search globally every n reservations
#define SKIP_NEAR_FREQ 4U

/// CPU-local data of the currently reserved tree
typedef struct reserved {
	/// Number of free frames in the tree
	treeF_t free : LLFREE_TREE_ORDER + 1;
	/// Bitfield row index of reserved tree,
	/// used for identifying the reserved tree and as starting point
	/// for the next allocation
	uint64_t start_row : 62 - LLFREE_TREE_ORDER;
	/// true if there is a reserved tree
	bool present : 1;
} reserved_t;

/// Counts last frees in same tree
typedef struct local_history {
	/// Index of the last tree where a frame was freed
	uint64_t idx : 48;
	/// Number of frees in the same tree
	uint16_t frees : 16;
} local_history_t;

/// This represents the local CPU data
typedef struct __attribute__((aligned(LLFREE_CACHE_SIZE))) local {
	/// Counts last frees in same tree
	_Atomic(local_history_t) last;
	/// Reserved trees
	_Atomic(reserved_t) reserved[TREE_KINDS];
} local_t;

/// Initialize the per-cpu data
void ll_local_init(local_t *self);
/// Decrement the number of free frames
bool ll_reserved_dec(reserved_t *self, treeF_t free);
bool ll_reserved_dec_check(reserved_t *self, uint64_t tree_idx, treeF_t free);
/// Increment the number of free frames
bool ll_reserved_inc(reserved_t *self, uint64_t tree_idx, treeF_t free);
/// Try stealing a reserved tree
bool ll_steal(reserved_t *self, treeF_t min);
bool ll_steal_check(reserved_t *self, uint64_t tree_idx, treeF_t min);

/// Swap the reserved tree
bool ll_reserved_swap(reserved_t *self, reserved_t new);
/// Set the starting row index (if the the tree is the same as the current reserved tree)
bool ll_reserved_set_start(reserved_t *self, uint64_t start_row, bool force);

/// Updates the last-frees heuristic, returning true if the corresponding
/// tree should be reserved
bool ll_local_free_inc(local_t *self, uint64_t tree_idx);
