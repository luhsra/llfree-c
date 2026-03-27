#pragma once

#include "tree.h"
#include "llfree.h"
#include "utils.h"

/// Reservation heuristic: number of frees in the same tree before reserving it
#define LAST_FREES 4U

typedef struct local local_t;

/// Size of the local metadata for the given tiering.
size_t ll_local_size(const llfree_tiering_t *tiering);

/// Initialize the per-cpu data.
/// The tiers array will contain slices pointing into the metadata buffer.
void ll_local_init(local_t *self, const llfree_tiering_t *tiering);

/// Returns the allocated byte size of an initialized local block (aligned)
size_t ll_local_mem_size(const local_t *self);
/// Get the number of tiers
uint8_t ll_local_num_tiers(const local_t *self);

/// Returns the number of local slots for a given tier, or LLFREE_LOCAL_NONE if tier not configured.
size_t ll_local_tier_locals(const local_t *self, uint8_t tier);

/// Result of a local get/put operation
typedef struct local_result {
	bool success;
	bool present; /// was there a previous reservation?
	uint8_t tier; /// tier of the reservation
	treeF_t free; /// free count
	row_id_t start_row; /// bitfield row index of reserved tree
} local_result_t;

static inline ll_unused local_result_t local_ok(uint8_t tier, treeF_t free,
				      row_id_t start_row)
{
	return (local_result_t){ .success = true,
				 .present = true,
				 .tier = tier,
				 .free = free,
				 .start_row = start_row };
}
static inline ll_unused local_result_t local_fail(uint8_t tier, bool present,
					treeF_t free, row_id_t start_row)
{
	return (local_result_t){ .success = false,
				 .present = present,
				 .tier = tier,
				 .free = free,
				 .start_row = start_row };
}

/// Decrement the number of free frames for the given (tier, index).
/// tree_idx: if present, only succeed if the reserved tree matches.
local_result_t ll_local_get(local_t *self, uint8_t tier, size_t index,
			    tree_id_optional_t tree_idx, treeF_t frames);

/// Increment the number of free frames for the given (tier, index).
bool ll_local_put(local_t *self, uint8_t tier, size_t index,
		  tree_id_t tree_idx,
		  treeF_t frames);

/// Update the starting row for the given (tier, index).
local_result_t ll_local_set_start(local_t *self, uint8_t tier, size_t index,
				  row_id_t start_row);

/// Swap (tier, index) with a new tree (returns the old reservation).
local_result_t ll_local_swap(local_t *self, uint8_t tier, size_t index,
			     tree_id_t new_tree_idx, treeF_t new_free);

/// Steal without demoting: find a slot where policy returns MATCH or STEAL,
/// decrement its free counter, allocate from there.
/// On success, result.{tier, free, start_row} describe the stolen slot.
local_result_t ll_local_steal(local_t *self, uint8_t tier, size_t index,
			      tree_id_optional_t tree_idx, treeF_t frames,
			      llfree_policy_fn policy);

/// Result of ll_local_demote_any.
typedef struct demote_any_result {
	bool found;
	row_id_t row; /// row to allocate from
	row_id_optional_t old_row; /// old tree in the requesting local, if present
	uint8_t old_tier; /// tier of old tree (= requesting tier)
	treeF_t old_free;
} demote_any_result_t;

/// Demote from another tier's local slot
/// Finds a target slot where policy(tier, target_tier, frames) == DEMOTE,
/// atomically clears it (checking it has enough free),
/// swaps the decremented tree into the requesting local.
/// Returns the row to allocate from and the old requesting local for unreservation.
demote_any_result_t ll_local_demote_any(local_t *self, uint8_t tier,
					ll_optional_t index,
					tree_id_optional_t tree_idx,
					treeF_t frames,
					llfree_policy_fn policy);

/// Update the last-frees heuristic; returns true if the tree should be reserved
bool ll_local_free_inc(local_t *self, uint8_t tier, size_t index,
		       tree_id_t tree_idx);

/// Drain a single local slot (clear reservation).
/// Returns the old reservation for the caller to unreserve the global tree.
local_result_t ll_local_drain(local_t *self, uint8_t tier, size_t index);

/// Return stats summed over all slots
ll_tree_stats_t ll_local_stats(const local_t *self);

/// Return stats for the slot whose reserved tree matches tree_idx
local_result_t ll_local_stats_at(const local_t *self, tree_id_t tree_idx);

/// Debug print the local data
void ll_local_print(const local_t *self, size_t indent);
/// Validate the local data
void ll_local_validate(const local_t *self, const llfree_t *llfree,
		       void (*validate_tree)(const llfree_t *llfree,
					     local_result_t res));
