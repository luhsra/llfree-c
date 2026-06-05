#pragma once

#include "tree.h"
#include "llfree.h"
#include "utils.h"

/// Manages the tree array
/// Wraps the atomic tree entry array and provides operations on it.
typedef struct trees {
	_Atomic(tree_t) *entries;
	size_t len;
	uint8_t default_cluster;
} trees_t;

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
		trees_init_fn init_fn, void *init_ctx, uint8_t default_cluster);

/// Return pointer to raw metadata buffer
uint8_t *trees_metadata(const trees_t *self);

/// Load a single tree entry (atomic read)
tree_t trees_load(const trees_t *self, tree_id_t idx);

/// Decrement free counter and update cluster via check callback.
/// On success, writes the resulting cluster to *out_cluster.
bool trees_steal(trees_t *self, tree_id_t idx, treeF_t frames,
	       uint8_t *cluster, llfree_policy_fn policy);

/// Increment free counter; resets cluster to default when tree becomes fully free.
void trees_put(trees_t *self, tree_id_t idx, treeF_t frames,
	       llfree_policy_fn policy);

/// Reserve a tree (Match/Demote) or steal from it (Steal), atomic.
/// On reserve: sets reserved=true, free=0, cluster=requested cluster.
/// On steal: decrements free counter, keeps existing cluster.
/// Returns true on success, false if tree is reserved or insufficient free.
/// *out_reserved: true if reserved, false if stolen.
/// *out_free: old free count on success.
/// *out_cluster: resulting cluster.
bool trees_reserve_or_steal(trees_t *self, tree_id_t idx, treeF_t frames,
			    llfree_policy_fn policy, uint8_t cluster,
			    bool *out_reserved, treeF_t *out_free,
			    uint8_t *out_cluster);

/// Unreserve a tree and add free frames back; handles cluster demotion via policy.
void trees_unreserve(trees_t *self, tree_id_t idx, treeF_t free, uint8_t cluster,
		     llfree_policy_fn policy);

/// Steal the global free counter from a reserved tree (synchronization).
/// Returns true if successful, writing the stolen count to *out_stolen.
bool trees_sync_steal(trees_t *self, tree_id_t idx, treeF_t min,
		      treeF_t *out_stolen);

/// Callback for tree search: attempt operation at given tree index.
/// Return LLFREE_ERR_MEMORY to continue searching, anything else to stop.
typedef llfree_result_t (*trees_access_fn)(tree_id_t idx, void *ctx);

/// Callback to fetch current free frames for online operation.
typedef treeF_t (*trees_fetch_free_fn)(tree_id_t idx, void *ctx);

/// Linear alternating search from start.
llfree_result_t trees_search(const trees_t *self, tree_id_t start,
			     size_t offset, size_t len, trees_access_fn cb,
			     void *ctx);

/// Rate callback for trees_search_best: evaluates a tree and returns its policy.
/// Return LLFREE_POLICY_MATCH with priority UINT8_MAX for immediate try,
/// LLFREE_POLICY_MATCH with lower priority for best-fit, LLFREE_POLICY_DEMOTE
/// for acceptable but low-priority candidates, LLFREE_POLICY_INVALID to skip.
typedef llfree_policy_t (*trees_rate_fn)(uint8_t target_cluster, treeF_t free,
					 void *rate_args);

/// Best-fit search: evaluate each tree via rate(), try perfect matches
/// immediately, then collect top N candidates by priority and try them.
#define TREES_SEARCH_BEST 8
llfree_result_t trees_search_best(const trees_t *self, tree_id_t start,
				  size_t offset, size_t len, trees_rate_fn rate,
				  void *rate_args, trees_access_fn cb,
				  void *ctx);

/// Compute tree statistics over the entire array
ll_tree_stats_t trees_stats(const trees_t *self);

/// Load stats for a specific tree entry
void trees_stats_at(const trees_t *self, tree_id_t idx, uint8_t *cluster,
		    treeF_t *free, bool *reserved);

/// Change tree metadata according to matcher and change.
/// Returns LLFREE_ERR_OK on success, LLFREE_ERR_MEMORY on no matching tree.
llfree_result_t trees_change(trees_t *self, llfree_tree_match_t matcher,
			     llfree_tree_change_t change,
			     trees_fetch_free_fn fetch_free, void *fetch_ctx);

/// Print all tree entries
void trees_print(const trees_t *self, size_t indent);
