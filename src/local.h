#pragma once

#include "llfree.h"
#include "tree.h"
#include "utils.h"

/// Reservation heuristic: number of frees in the same tree before reserving it
#define LAST_FREES 4U

typedef struct local local_t;

/// Size of the local data for the given configuration
size_t ll_local_size(const llkind_desc_t *kinds);

/// Initialize the per-cpu data
void ll_local_init(local_t *self, const llkind_desc_t *kinds);
/// Get the number of entries
size_t ll_local_len(const local_t *self);
/// Get the kind of an entry
llkind_t ll_local_kind(const local_t *self, size_t local);

/// Result of a local get/put operation, usually the previous value
typedef struct local_result {
	bool success;
	bool present;
	tree_t tree;
	size_t start_row;
} local_result_t;
static inline local_result_t ll_unused local_result(bool success, bool present,
						    size_t start_row,
						    tree_t tree)
{
	return (local_result_t){ success, present, tree, start_row };
}

/// Decrement the number of free frames
/// - Optionally check if the tree index matches
local_result_t ll_local_get(local_t *self, size_t local, ll_optional_t tree_idx,
			    treeF_t free);
bool ll_local_can_get(local_t *self, size_t local, ll_optional_t tree_idx,
		      treeF_t free);

/// Increment the number of free frames
bool ll_local_put(local_t *self, size_t local, size_t tree_idx, treeF_t free);

/// Set the starting row index
void ll_local_set_start(local_t *self, size_t local, uint64_t row);

/// Steal from another compatible local tree (no downgrade necessary)
local_result_t ll_local_steal(local_t *self, size_t local,
			      ll_optional_t tree_idx, treeF_t free);
/// Find higher kind and steal and downgrade it to the current kind
local_result_t ll_local_steal_downgrade(local_t *self, size_t local,
					ll_optional_t tree_idx, treeF_t free);

/// Try to swap the local tree with a new one
local_result_t ll_local_swap(local_t *self, size_t local, size_t tree_idx,
			     treeF_t free);

/// Updates the last-frees heuristic, returning true if the corresponding
/// tree should be reserved
bool ll_local_free_inc(local_t *self, size_t local, size_t tree_idx);

/// Unreserve all local trees for an entry
local_result_t ll_local_drain(local_t *self, size_t local);

/// Return the number of free frames for all entries
ll_stats_t ll_local_stats(const local_t *self);
/// Return the number of free frames for all entries
ll_stats_t ll_local_stats_at(const local_t *self, size_t tree_idx);

/// Get the index of the last reclaimed tree for an entry
size_t ll_local_reclaimed(const local_t *self, size_t local);
void ll_local_set_reclaimed(local_t *self, size_t local, size_t reclaimed_idx);

/// Debug print the local data
void ll_local_print(const local_t *self, size_t indent);
/// Validate the local data, checking for consistency
void ll_local_validate(const local_t *self, const llfree_t *llfree,
		       void (*validate_tree)(const llfree_t *llfree,
					     local_result_t res));
