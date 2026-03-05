#pragma once

#include "tree.h"
#include "llfree.h"
#include "utils.h"

/// Reservation heuristic: number of frees in the same tree before reserving it
#define LAST_FREES 4U

typedef struct local local_t;

/// Size of the local metadata for the given tiering.
/// Total slots = sum(tiering->tiers[i].count).
size_t ll_local_size(const llfree_tiering_t *tiering);

/// Initialize the per-cpu data.
/// Entries are laid out flat: [tier0_slot0..tier0_slot(count0-1), tier1_slot0..]
void ll_local_init(local_t *self, const llfree_tiering_t *tiering);

/// Get the total number of local slots
size_t ll_local_len(const local_t *self);
/// Returns the allocated byte size of an initialized local block (aligned)
size_t ll_local_mem_size(const local_t *self);
/// Get the number of tiers
uint8_t ll_local_num_tiers(const local_t *self);
/// Get the tier of a slot
uint8_t ll_local_tier(const local_t *self, size_t slot);

/// Result of a local get/put operation
typedef struct local_result {
	bool success;
	bool present; /// was there a previous reservation?
	uint8_t tier; /// tier of the reservation
	treeF_t free; /// free count
	uint64_t start_row; /// bitfield row index of reserved tree
} local_result_t;

static inline local_result_t local_ok(uint8_t tier, treeF_t free,
				      uint64_t start_row)
{
	return (local_result_t){ .success = true,
				 .present = true,
				 .tier = tier,
				 .free = free,
				 .start_row = start_row };
}
static inline local_result_t local_fail(uint8_t tier, bool present,
					treeF_t free, uint64_t start_row)
{
	return (local_result_t){ .success = false,
				 .present = present,
				 .tier = tier,
				 .free = free,
				 .start_row = start_row };
}

/// Decrement the number of free frames from slot idx.
/// tree_idx: if present, only succeed if the reserved tree matches.
local_result_t ll_local_get(local_t *self, size_t slot,
			    ll_optional_t tree_idx, treeF_t frames);

/// Increment the number of free frames for slot idx.
bool ll_local_put(local_t *self, size_t slot, size_t tree_idx, treeF_t frames);

/// Update the starting row for slot idx (keeping the same tree).
local_result_t ll_local_set_start(local_t *self, size_t slot,
				  uint64_t start_row);

/// Swap slot idx with a new tree (returns the old reservation).
local_result_t ll_local_swap(local_t *self, size_t slot, size_t new_tree_idx,
			     treeF_t new_free);

/// Steal from another local slot using the given policy:
/// - First tries to steal from a same-tier slot (Policy::MATCH)
/// - Then from a compatible Steal slot
/// - Returns success=false if nothing found.
/// On success, result.{tier, free, start_row} describe the stolen slot.
local_result_t ll_local_steal(local_t *self, size_t slot,
			      ll_optional_t tree_idx, treeF_t frames,
			      llfree_policy_fn policy);

/// Steal a slot where policy returns DEMOTE, atomically clearing it.
/// Returns the taken slot's info so the caller can change the global tree tier.
/// If old_out != NULL, it receives the taken slot as-is (original tier).
local_result_t ll_local_steal_demote(local_t *self, size_t slot,
				     llfree_policy_fn policy,
				     local_result_t *old_out);

/// Update the last-frees heuristic; returns true if the tree should be reserved
bool ll_local_free_inc(local_t *self, size_t slot, size_t tree_idx);

/// Drain a single local slot (clear reservation without updating global tree)
void ll_local_drain(local_t *self, size_t slot);

/// Return stats summed over all slots
ll_tree_stats_t ll_local_stats(const local_t *self, ll_tier_stats_t *tiers,
			       size_t tier_len);

/// Return stats for the slot whose reserved tree matches tree_idx
local_result_t ll_local_stats_at(const local_t *self, size_t tree_idx);

/// Debug print the local data
void ll_local_print(const local_t *self, size_t indent);
/// Validate the local data
void ll_local_validate(const local_t *self, const llfree_t *llfree,
		       void (*validate_tree)(const llfree_t *llfree,
					     local_result_t res));
