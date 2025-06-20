#pragma once

#include "tree.h"
#include "utils.h"

/// Reservation heuristic: number of frees in the same tree before reserving it
#define LAST_FREES 4U

typedef struct local local_t;

/// Size of a single CPU-local entry
size_t ll_local_size(size_t cores);

/// Initialize the per-cpu data
void ll_local_init(local_t *self, size_t cores);
/// Get the number of cores
size_t ll_local_cores(local_t *self);

/// Result of a local get/put operation, usually the previous value
typedef struct local_result {
	bool success;
	bool present;
	size_t start_row;
	tree_t tree;
} local_result_t;
static inline local_result_t local_result(bool success, bool present,
					  size_t start_row, tree_t tree)
{
	return (local_result_t){ success, present, start_row, tree };
}

/// Decrement the number of free frames
/// - Optionally check if the tree index matches
local_result_t ll_local_get(local_t *self, size_t core, tree_change_t change,
			    optional_size_t tree_idx);

/// Increment the number of free frames
bool ll_local_put(local_t *self, size_t core, tree_change_t change,
		  size_t tree_idx);

/// Set the starting row index
local_result_t ll_local_set_start(local_t *self, size_t core,
				  tree_change_t previous_change,
				  uint64_t start_row);

/// Try stealing from another tree kind
/// If not demote, steal from a lower kind, else from a higher kind
/// TODO: Here we have a short period where the tree kind might not be accurate!
local_result_t ll_local_steal(local_t *self, size_t core, tree_change_t change,
			      bool demote, optional_size_t tree_idx);

/// Try to swap the local tree with a new one
local_result_t ll_local_swap(local_t *self, size_t core,
			     tree_change_t previous_change, size_t new_idx,
			     tree_t new_tree);

/// Demote the tree to a lower kind according to the previous change
/// This tries to also take the reservation, returns if successful
/// Otherwise, the global tree has to be demoted!
local_result_t ll_local_demote(local_t *self, size_t core,
			       tree_change_t previous_change, size_t tree_idx);

/// Updates the last-frees heuristic, returning true if the corresponding
/// tree should be reserved
bool ll_local_free_inc(local_t *self, size_t core, size_t tree_idx);

/// Unreserve all local trees for a core
void ll_local_drain(local_t *self, size_t core);

/// Return the number of free frames for all core
size_t ll_local_free_frames(local_t *self);

/// Get the index of the last reclaimed tree for a core
size_t ll_local_reclaimed(local_t *self, size_t core);
void ll_local_set_reclaimed(local_t *self, size_t core, size_t reclaimed_idx);

/// Debug print the local data
void ll_local_print(local_t *self, size_t indent);
/// Validate the local data, checking for consistency
void ll_local_validate(local_t *self, llfree_t *llfree,
		       void (*validate_tree)(llfree_t *llfree,
					     local_result_t res));
