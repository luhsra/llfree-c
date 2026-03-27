#pragma once

#include "tree.h"
#include "llfree.h"
#include "utils.h"

/// Manages the tree array
/// Wraps the atomic tree entry array and provides operations on it.
typedef struct trees {
	_Atomic(tree_t) *entries;
	size_t len;
	uint8_t default_tier;
} trees_t;

/// Minimum free count for partial-tree heuristics
#define TREES_MIN_FREE (LLFREE_TREE_SIZE / 16)

/// Size of the metadata buffer needed for the tree array
static inline ll_unused size_t trees_metadata_size(size_t frames)
{
	size_t tree_len = div_ceil(frames, LLFREE_TREE_SIZE);
	return align_up(sizeof(tree_t) * tree_len, LLFREE_CACHE_SIZE);
}

/// Initialize callback: given tree start frame id, return free frame count
typedef treeF_t (*trees_init_fn)(frame_id_t tree_start_frame, void *ctx);

/// Initialize the tree array.
/// If init_fn is NULL, entries are assumed already valid (INIT_NONE).
void trees_init(trees_t *self, size_t frames, uint8_t *buffer,
		trees_init_fn init_fn, void *init_ctx, uint8_t default_tier);

/// Return pointer to raw metadata buffer
uint8_t *trees_metadata(const trees_t *self);

/// Load a single tree entry (atomic read)
tree_t trees_load(const trees_t *self, tree_id_t idx);

/// Decrement free counter and update tier via check callback.
/// On success, writes the resulting tier to *out_tier.
bool trees_get(trees_t *self, tree_id_t idx, treeF_t frames,
	       tree_check_fn check, void *args, uint8_t *out_tier);

/// Increment free counter; resets tier to default when tree becomes fully free.
void trees_put(trees_t *self, tree_id_t idx, treeF_t frames,
	       llfree_policy_fn policy);

/// Reserve a tree if check permits.
/// On success, writes the old free count to *out_free and tier to *out_tier.
bool trees_reserve(trees_t *self, tree_id_t idx, tree_check_fn check,
		   void *args,
		   treeF_t *out_free, uint8_t *out_tier);

/// Unreserve a tree and add free frames back; handles tier demotion via policy.
void trees_unreserve(trees_t *self, tree_id_t idx, treeF_t free,
		     uint8_t tier,
		     llfree_policy_fn policy);

/// Steal the global free counter from a reserved tree (synchronization).
/// Returns true if successful, writing the stolen count to *out_stolen.
bool trees_sync_steal(trees_t *self, tree_id_t idx, treeF_t min,
		      treeF_t *out_stolen);

/// Increment free counter and optionally reserve the tree (free-reserve heuristic).
/// may_reserve: whether reservation should be attempted.
/// On return, *did_reserve indicates if reservation occurred.
/// When reserved, *out_old_free is the free count before the put.
void trees_put_or_reserve(trees_t *self, tree_id_t idx, treeF_t frames,
			  uint8_t tier, bool may_reserve,
			  llfree_policy_fn policy, bool *did_reserve,
			  treeF_t *out_old_free);

/// Callback for tree search: attempt operation at given tree index.
/// Return LLFREE_ERR_MEMORY to continue searching, anything else to stop.
typedef llfree_result_t (*trees_access_fn)(tree_id_t idx, void *ctx);

/// Callback to fetch current free frames for online operation.
typedef treeF_t (*trees_fetch_free_fn)(tree_id_t idx, void *ctx);

/// Linear alternating search from start.
llfree_result_t trees_search(const trees_t *self, size_t start, size_t offset,
			     size_t len, trees_access_fn cb, void *ctx);

/// Best-fit search: collect N best candidates by policy priority, then try them.
#define TREES_SEARCH_BEST 3
llfree_result_t trees_search_best(const trees_t *self, uint8_t tier,
				  size_t start, size_t offset, size_t len,
				  treeF_t min_free, llfree_policy_fn policy,
				  trees_access_fn cb, void *ctx);

/// Compute tree statistics over the entire array
ll_tree_stats_t trees_stats(const trees_t *self);

/// Load stats for a specific tree entry
void trees_stats_at(const trees_t *self, tree_id_t idx, uint8_t *tier,
		    treeF_t *free, bool *reserved);

/// Change tree metadata according to matcher and change.
/// Returns LLFREE_ERR_OK on success, LLFREE_ERR_MEMORY on no matching tree.
llfree_result_t trees_change(trees_t *self, llfree_tree_match_t matcher,
			     llfree_tree_change_t change,
			     trees_fetch_free_fn fetch_free, void *fetch_ctx);

/// Print all tree entries
void trees_print(const trees_t *self, size_t indent);
