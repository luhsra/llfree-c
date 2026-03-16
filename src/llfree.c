#include "llfree_inner.h"

#include "llfree.h"
#include "child.h"
#include "llfree_platform.h"
#include "llfree_types.h"
#include "trees.h"
#include "local.h"
#include "lower.h"
#include "utils.h"

/// Callback for trees_init: reads child counters from the lower allocator
static treeF_t init_tree_cb(size_t tree_start_frame, void *ctx)
{
	lower_t *lower = (lower_t *)ctx;
	size_t tree_idx = tree_start_frame / LLFREE_TREE_SIZE;
	treeF_t sum = 0;
	for (size_t child_idx = 0; child_idx < LLFREE_TREE_CHILDREN;
	     ++child_idx) {
		child_t child = atom_load(
			&lower->children[tree_idx].entries[child_idx]);
		sum += child.free;
	}
	return sum;
}

llfree_meta_size_t llfree_metadata_size(const llfree_tiering_t *tiering,
					size_t frames)
{
	llfree_meta_size_t meta = {
		.llfree = sizeof(llfree_t),
		.trees = trees_metadata_size(frames),
		.local = ll_local_size(tiering),
		.lower = lower_metadata_size(frames),
	};
	return meta;
}

llfree_meta_size_t llfree_metadata_size_of(const llfree_t *self)
{
	assert(self != NULL);
	return (llfree_meta_size_t){
		.llfree = sizeof(llfree_t),
		.trees = trees_metadata_size(self->lower.frames),
		.local = ll_local_mem_size(self->local),
		.lower = lower_metadata_size(self->lower.frames),
	};
}

static ll_unused bool check_meta(llfree_meta_t meta, llfree_meta_size_t sizes)
{
	if ((size_t)meta.local % LLFREE_CACHE_SIZE != 0 ||
	    (size_t)meta.trees % LLFREE_CACHE_SIZE != 0 ||
	    (size_t)meta.lower % LLFREE_CACHE_SIZE != 0)
		return false;
	// no overlap!
	return (meta.local + sizes.local <= meta.lower ||
		meta.lower + sizes.lower <= meta.local) &&
	       (meta.local + sizes.local <= meta.trees ||
		meta.trees + sizes.trees <= meta.local) &&
	       (meta.lower + sizes.lower <= meta.trees ||
		meta.trees + sizes.trees <= meta.lower);
}

llfree_result_t llfree_init(llfree_t *self, size_t frames, uint8_t init,
			    llfree_meta_t meta, const llfree_tiering_t *tiering)
{
	assert(self != NULL);
	assert(tiering != NULL);
	assert(tiering->num_tiers >= 1 &&
	       tiering->num_tiers <= LLFREE_MAX_TIERS);
	assert(check_meta(meta, llfree_metadata_size(tiering, frames)));

	if (init >= LLFREE_INIT_MAX) {
		llfree_info("Invalid init mode %d", init);
		return llfree_err(LLFREE_ERR_INIT);
	}
	if (frames < MIN_PAGES || frames > MAX_PAGES) {
		llfree_info("Invalid size %" PRIu64, (uint64_t)frames);
		return llfree_err(LLFREE_ERR_INIT);
	}

	llfree_result_t res =
		lower_init(&self->lower, frames, init, meta.lower);
	if (!llfree_is_ok(res))
		return res;

	// Initialize trees via trees_init (replaces manual init_trees)
	trees_init_fn init_fn = (init != LLFREE_INIT_NONE) ? init_tree_cb :
							     NULL;
	trees_init(&self->trees, frames, meta.trees, init_fn, &self->lower,
		   tiering->default_tier);

	self->local = (local_t *)meta.local;
	ll_local_init(self->local, tiering);

	self->policy = tiering->policy;
	self->num_tiers = (uint8_t)tiering->num_tiers;

	return llfree_ok(0, 0);
}

llfree_meta_t llfree_metadata(const llfree_t *self)
{
	assert(self != NULL);
	llfree_meta_t meta = {
		.local = (uint8_t *)self->local,
		.trees = trees_metadata(&self->trees),
		.lower = lower_metadata(&self->lower),
	};
	return meta;
}

// == Check functions ==
// These are callbacks for tree_check_fn (uint8_t tree_tier, treeF_t frames, void *args)
// Each matches a specific Rust closure pattern.

struct check_args {
	uint8_t tier;
	p_range_t free;
	llfree_policy_fn policy;
};

/// Used for get_matching_reserve: Match -> requested, Demote-if-full -> requested.
/// Matches Rust get_matching_reserve closure.
static uint8_t check_reserve_tree(uint8_t tree_tier, treeF_t frames, void *args)
{
	struct check_args *a = (struct check_args *)args;
	if (frames < a->free.min || frames > a->free.max)
		return LLFREE_TIER_NONE;
	llfree_policy_t p = a->policy(a->tier, tree_tier, frames);
	if (p.type == LLFREE_POLICY_MATCH)
		return a->tier;
	if (p.type == LLFREE_POLICY_DEMOTE && frames == LLFREE_TREE_SIZE)
		return a->tier;
	return LLFREE_TIER_NONE;
}

/// Used for get_matching_global: Match only -> target tier.
/// Matches Rust get_matching_global closure.
static uint8_t check_global_matching_tree(uint8_t tree_tier, treeF_t frames,
					  void *args)
{
	struct check_args *a = (struct check_args *)args;
	if (frames < a->free.min || frames > a->free.max)
		return LLFREE_TIER_NONE;
	llfree_policy_t p = a->policy(a->tier, tree_tier, frames);
	if (p.type == LLFREE_POLICY_MATCH)
		return tree_tier;
	return LLFREE_TIER_NONE;
}

/// Used for get_demoting: Demote only -> requested tier.
/// Matches Rust get_demoting closure.
static uint8_t check_demote_tree(uint8_t tree_tier, treeF_t frames, void *args)
{
	struct check_args *a = (struct check_args *)args;
	if (frames < a->free.min || frames > a->free.max)
		return LLFREE_TIER_NONE;
	llfree_policy_t p = a->policy(a->tier, tree_tier, frames);
	if (p.type == LLFREE_POLICY_DEMOTE)
		return a->tier;
	return LLFREE_TIER_NONE;
}

/// Used for get_at global fallback: Match/Steal -> target, Demote -> requested.
/// Matches Rust get_at closure.
static uint8_t check_get_at_tree(uint8_t tree_tier, treeF_t frames, void *args)
{
	struct check_args *a = (struct check_args *)args;
	if (frames < a->free.min || frames > a->free.max)
		return LLFREE_TIER_NONE;
	llfree_policy_t p = a->policy(a->tier, tree_tier, frames);
	switch (p.type) {
	case LLFREE_POLICY_MATCH:
	case LLFREE_POLICY_STEAL:
		return tree_tier;
	case LLFREE_POLICY_DEMOTE:
		return a->tier;
	default:
		return LLFREE_TIER_NONE;
	}
}

// == Core allocation helpers ==

typedef struct reserve_args {
	llfree_t *self;
	uint8_t order;
	uint8_t tier;
	size_t local;
	tree_check_fn check;
	void *check_args;
} reserve_args_t;

/// Swap out the currently reserved tree for a new one and write back the
/// free counter to the formerly reserved global tree.
static void swap_reserved(llfree_t *self, uint8_t tier, size_t index,
			  size_t new_idx, treeF_t new_free)
{
	llfree_debug("swap tier=%u index=%zu idx=%zu free=%" PRIuS, tier, index,
		     new_idx, (size_t)new_free);
	local_result_t old =
		ll_local_swap(self->local, tier, index, new_idx, new_free);
	assert(old.success);
	if (old.present) {
		trees_unreserve(&self->trees, tree_from_row(old.start_row),
				old.free, old.tier, self->policy);
	}
}

/// Reserves a new tree and allocates from it.
/// Only if the allocation succeeds, the tree is fully reserved.
static llfree_result_t get_reserve(size_t idx, void *ctx)
{
	reserve_args_t *rargs = (reserve_args_t *)ctx;
	llfree_t *self = rargs->self;

	treeF_t old_free;
	uint8_t target_tier;
	if (!trees_reserve(&self->trees, idx, rargs->check, rargs->check_args,
			   &old_free, &target_tier))
		return llfree_err(LLFREE_ERR_MEMORY);

	size_t tier_len = ll_local_tier_locals(self->local, target_tier);
	if (tier_len == 0 || tier_len == LLFREE_LOCAL_NONE) {
		trees_unreserve(&self->trees, idx, old_free, target_tier,
				self->policy);
		llfree_warn("no locals for tier %u", target_tier);
		return llfree_err(LLFREE_ERR_MEMORY);
	}
	size_t local = rargs->local % tier_len;

	llfree_result_t res = lower_get(&self->lower, frame_from_tree(idx),
					rargs->order, ll_none());

	if (llfree_is_ok(res)) {
		treeF_t new_free = old_free - (treeF_t)(1u << rargs->order);
		swap_reserved(self, target_tier, local, idx, new_free);
		res.tier = target_tier;
	} else {
		llfree_debug("reserve failed tier=%u index=%zu idx=%zu",
			     rargs->tier, rargs->local, idx);
		trees_unreserve(&self->trees, idx, old_free, target_tier,
				self->policy);
	}
	return res;
}

/// Reserves and allocates from a new tree, searching near `start`.
static llfree_result_t get_matching_reserve(llfree_t *self, uint8_t tier,
					    size_t local, uint8_t order,
					    uint64_t start)
{
	assert(start < self->trees.len);

	llfree_result_t res;
	const size_t cl_trees = LLFREE_CACHE_SIZE / sizeof(tree_t);
	size_t near = LL_MAX(self->trees.len / 16, cl_trees / 4);
	start = align_down(start, next_pow2(2 * near));

	struct check_args check_args = {
		.tier = tier,
		.free = p_range(0, 0),
		.policy = self->policy,
	};
	reserve_args_t args = { .self = self,
				.order = order,
				.tier = tier,
				.local = local,
				.check = check_reserve_tree,
				.check_args = &check_args };

	llfree_debug("reserve tier=%u index=%zu o=%d", tier, local, order);

	if (order < LLFREE_HUGE_ORDER) {
		// Avoid fragmenting fully free trees
		check_args.free = p_range(1 << order, LLFREE_TREE_SIZE - 1);

		res = trees_search_best(&self->trees, tier, start, 1, near,
					1 << order, self->policy, get_reserve,
					&args);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;

		res = trees_search(&self->trees, start, 1, self->trees.len,
				   get_reserve, &args);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;
	}

	// Any tree (including fully free ones)
	check_args.free = p_range(1 << order, LLFREE_TREE_SIZE);
	res = trees_search(&self->trees, start, 0, self->trees.len, get_reserve,
			   &args);
	return res;
}

/// Synchronize local free counter with the global counter (steal from global).
static bool sync_with_global(llfree_t *self, uint8_t tier, size_t index,
			     treeF_t needed, local_result_t old)
{
	assert(self != NULL);
	assert(old.present);

	if (old.free >= needed)
		return false;

	size_t tree_idx = tree_from_row(old.start_row);
	treeF_t steal_min = needed - old.free;

	treeF_t stolen;
	if (!trees_sync_steal(&self->trees, tree_idx, steal_min, &stolen))
		return false;

	if (!ll_local_put(self->local, tier, index, tree_idx, stolen)) {
		llfree_warn("sync local failed tier=%u index=%zu idx=%zu", tier,
			    index, tree_idx);
		trees_put(&self->trees, tree_idx, stolen, self->policy);
		return false;
	}
	llfree_debug("sync success tier=%u index=%zu free=%" PRIuS, tier, index,
		     (size_t)stolen);
	return true;
}

static llfree_result_t get_from_local(llfree_t *self, uint8_t tier,
				      size_t index, uint8_t order,
				      treeF_t frames, local_result_t *old)
{
	*old = ll_local_get(self->local, tier, index, ll_none(), frames);
	if (old->success) {
		llfree_result_t res = lower_get(&self->lower,
						frame_from_row(old->start_row),
						order, ll_none());
		if (llfree_is_ok(res)) {
			uint64_t start_row = row_from_frame(res.frame);
			if (old->start_row != start_row) {
				*old = ll_local_set_start(self->local, tier,
							  index, start_row);
			}
			return llfree_ok(res.frame, tier);
		}

		trees_put(&self->trees, tree_from_row(old->start_row), frames,
			  self->policy);
		return res;
	}

	if (old->present && sync_with_global(self, tier, index, frames, *old)) {
		return llfree_err(LLFREE_ERR_RETRY);
	}

	return llfree_err(LLFREE_ERR_MEMORY);
}

/// Allocate from a global tree, decrementing the tree counter.
typedef struct get_global_args {
	llfree_t *self;
	uint8_t tier;
	uint8_t order;
	tree_check_fn check;
	void *check_args;
} get_global_args_t;

static llfree_result_t get_global(size_t idx, void *ctx)
{
	get_global_args_t *rargs = (get_global_args_t *)ctx;
	llfree_t *self = rargs->self;
	treeF_t frames = (treeF_t)(1u << rargs->order);

	uint8_t new_tier;
	if (!trees_get(&self->trees, idx, frames, rargs->check,
		       rargs->check_args, &new_tier))
		return llfree_err(LLFREE_ERR_MEMORY);

	llfree_result_t res = lower_get(&self->lower, frame_from_tree(idx),
					rargs->order, ll_none());
	if (!llfree_is_ok(res)) {
		trees_put(&self->trees, idx, frames, self->policy);
		return res;
	}
	return llfree_ok(res.frame, new_tier);
}

/// Steal from any local reservation (matches Rust steal_local).
static llfree_result_t steal_local(llfree_t *self,
				   const llfree_request_t *request,
				   ll_optional_t frame)
{
	treeF_t frames = (treeF_t)(1u << request->order);
	ll_optional_t tree_idx = frame.present ?
					 ll_some(tree_from_frame(frame.value)) :
					 ll_none();
	size_t index = (request->local == LLFREE_LOCAL_NONE) ? 0 :
							       request->local;

	local_result_t res = ll_local_steal(self->local, request->tier, index,
					    tree_idx, frames, self->policy);
	if (res.success) {
		ll_optional_t lower_frame = frame;
		llfree_result_t res2 = lower_get(&self->lower,
						 frame_from_row(res.start_row),
						 request->order, lower_frame);
		if (llfree_is_ok(res2))
			return llfree_ok(res2.frame, res.tier);
		trees_put(&self->trees, tree_from_row(res.start_row), frames,
			  self->policy);
		if (res2.error != LLFREE_ERR_MEMORY)
			return res2;
	}

	return llfree_err(LLFREE_ERR_MEMORY);
}

/// Try matching-only allocation: local retry + reserve (matches Rust get_matching).
static llfree_result_t
get_matching(llfree_t *self, const llfree_request_t *request, uint64_t *start)
{
	size_t tier_count = ll_local_tier_locals(self->local, request->tier);
	treeF_t frames = (treeF_t)(1u << request->order);

	if (request->local != LLFREE_LOCAL_NONE &&
	    tier_count != LLFREE_LOCAL_NONE && tier_count > 0 &&
	    tier_count < self->trees.len) {
		for (size_t i = 0; i < RETRIES; i++) {
			local_result_t old;
			llfree_result_t res = get_from_local(
				self, request->tier, request->local,
				request->order, frames, &old);
			if (old.present)
				*start = tree_from_row(old.start_row);

			if (res.error == LLFREE_ERR_RETRY)
				continue;
			if (res.error == LLFREE_ERR_MEMORY)
				break;
			return res;
		}
		return get_matching_reserve(self, request->tier, request->local,
					    request->order, *start);
	}

	// Global matching (no local reservations): Match only -> target tier
	struct check_args check_args = {
		.tier = request->tier,
		.free = p_range(1 << request->order, LLFREE_TREE_SIZE),
		.policy = self->policy,
	};
	get_global_args_t args = {
		.self = self,
		.tier = request->tier,
		.order = request->order,
		.check = check_global_matching_tree,
		.check_args = &check_args,
	};
	return trees_search(&self->trees, *start, 0, self->trees.len,
			    get_global, &args);
}

/// Demote from a local reservation (matches Rust demote_local).
static llfree_result_t demote_local(llfree_t *self,
				    const llfree_request_t *request,
				    ll_optional_t frame)
{
	treeF_t frames = (treeF_t)(1u << request->order);
	ll_optional_t tree_idx = frame.present ?
					 ll_some(tree_from_frame(frame.value)) :
					 ll_none();
	size_t index = (request->local == LLFREE_LOCAL_NONE) ? 0 :
							       request->local;

	demote_any_result_t dem =
		ll_local_demote_any(self->local, request->tier, index, tree_idx,
				    frames, self->policy);
	if (dem.found) {
		if (dem.old_present) {
			trees_unreserve(&self->trees,
					tree_from_row(dem.old_row),
					dem.old_free, dem.old_tier,
					self->policy);
		}

		ll_optional_t lower_frame = frame;
		llfree_result_t res = lower_get(&self->lower,
						frame_from_row(dem.row),
						request->order, lower_frame);
		if (llfree_is_ok(res))
			return llfree_ok(res.frame, request->tier);
		trees_put(&self->trees, tree_from_row(dem.row), frames,
			  self->policy);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;
	}
	return llfree_err(LLFREE_ERR_MEMORY);
}

/// Allocate with demotion (matches Rust get_demoting).
static llfree_result_t
get_demoting(llfree_t *self, const llfree_request_t *request, uint64_t start)
{
	size_t tier_count = ll_local_tier_locals(self->local, request->tier);
	struct check_args check_args = {
		.tier = request->tier,
		.free = p_range(1 << request->order, LLFREE_TREE_SIZE),
		.policy = self->policy,
	};

	if (request->local != LLFREE_LOCAL_NONE && tier_count > 0 &&
	    tier_count != LLFREE_LOCAL_NONE && tier_count < self->trees.len) {
		reserve_args_t args = { .self = self,
					.order = request->order,
					.tier = request->tier,
					.local = request->local,
					.check = check_demote_tree,
					.check_args = &check_args };
		llfree_result_t res = trees_search(&self->trees, start, 0,
						   self->trees.len, get_reserve,
						   &args);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;

		return demote_local(self, request, ll_none());
	}
	// Global path with demotion-only
	get_global_args_t args = { .self = self,
				   .tier = request->tier,
				   .order = request->order,
				   .check = check_demote_tree,
				   .check_args = &check_args };
	return trees_search(&self->trees, start, 0, self->trees.len, get_global,
			    &args);
}

static llfree_result_t llfree_get_at(llfree_t *self, size_t frame,
				     llfree_request_t request)
{
	assert(frame < self->lower.frames);
	size_t tree_idx = tree_from_frame(frame);
	treeF_t frames = (treeF_t)(1u << request.order);

	// Try local reservation first
	if (request.local != LLFREE_LOCAL_NONE) {
		for (size_t r = 0; r < RETRIES; r++) {
			local_result_t old = ll_local_get(
				self->local, request.tier, request.local,
				ll_some(tree_idx), frames);
			if (old.success) {
				llfree_result_t res = lower_get(&self->lower,
								frame,
								request.order,
								ll_some(frame));
				if (llfree_is_ok(res))
					return llfree_ok(res.frame, old.tier);
				trees_put(&self->trees, tree_idx, frames,
					  self->policy);
				return res;
			}
			if (old.present &&
			    sync_with_global(self, request.tier, request.local,
					     frames, old))
				continue;
			break;
		}
	}

	// Fallback to global (Match/Steal -> target, Demote -> requested)
	struct check_args check_args = {
		.tier = request.tier,
		.free = p_range(frames, LLFREE_TREE_SIZE),
		.policy = self->policy,
	};
	uint8_t new_tier;
	if (trees_get(&self->trees, tree_idx, frames, check_get_at_tree,
		      &check_args, &new_tier)) {
		llfree_result_t res = lower_get(&self->lower, frame,
						request.order, ll_some(frame));
		if (llfree_is_ok(res))
			return llfree_ok(res.frame, new_tier);
		trees_put(&self->trees, tree_idx, frames, self->policy);
		return res;
	}

	// Steal from local
	llfree_result_t res = steal_local(self, &request, ll_some(frame));
	if (res.error != LLFREE_ERR_MEMORY)
		return res;

	// Demote from local
	return demote_local(self, &request, ll_some(frame));
}

llfree_result_t llfree_get(llfree_t *self, ll_optional_t frame,
			   llfree_request_t request)
{
	assert(self != NULL);
	assert(request.order <= LLFREE_MAX_ORDER);

	if (frame.present) {
		return llfree_get_at(self, frame.value, request);
	}

	size_t tier_count = ll_local_tier_locals(self->local, request.tier);
	uint64_t start;
	if (tier_count > 0 && request.local != LLFREE_LOCAL_NONE) {
		start = self->trees.len / tier_count * request.local;
	} else {
		start = 0;
	}

	llfree_result_t res = get_matching(self, &request, &start);
	if (res.error != LLFREE_ERR_MEMORY)
		return res;

	res = get_demoting(self, &request, start);
	if (res.error != LLFREE_ERR_MEMORY)
		return res;

	llfree_warn("OOM");

	for (uint8_t t = 1; t < LLFREE_MAX_TIERS; t++) {
		uint8_t down_tier =
			(uint8_t)((LLFREE_MAX_TIERS + request.tier - t) %
				  LLFREE_MAX_TIERS);
		if (ll_local_tier_locals(self->local, down_tier) !=
		    LLFREE_LOCAL_NONE) {
			llfree_request_t down_req = request;
			down_req.tier = down_tier;
			if (request.local != LLFREE_LOCAL_NONE) {
				size_t down_count = ll_local_tier_locals(
					self->local, down_tier);
				if (request.local >= down_count)
					down_req.local = LLFREE_LOCAL_NONE;
			}
			res = get_matching(self, &down_req, &start);
			if (res.error != LLFREE_ERR_MEMORY)
				return res;
		}
	}

	res = steal_local(self, &request, ll_none());
	if (res.error != LLFREE_ERR_MEMORY)
		return res;

	return llfree_err(LLFREE_ERR_MEMORY);
}

llfree_result_t llfree_put(llfree_t *self, uint64_t frame,
			   llfree_request_t request)
{
	assert(self != NULL);
	assert(request.order <= LLFREE_MAX_ORDER);
	assert(frame < self->lower.frames);

	llfree_result_t res = lower_put(&self->lower, frame, request.order);
	if (!llfree_is_ok(res)) {
		llfree_info("lower err %" PRIu64, (uint64_t)res.error);
		return res;
	}

	size_t tree_idx = tree_from_frame(frame);
	uint8_t tier = request.tier;
	treeF_t frames = (treeF_t)(1u << request.order);

	if (request.local != LLFREE_LOCAL_NONE) {
		bool may_reserve = LLFREE_ENABLE_FREE_RESERVE &&
				   ll_local_free_inc(self->local, request.tier,
						     request.local, tree_idx);

		if (ll_local_put(self->local, request.tier, request.local,
				 tree_idx, frames)) {
			return llfree_ok(0, 0);
		}

		bool did_reserve;
		treeF_t old_free;
		trees_put_or_reserve(&self->trees, tree_idx, frames, tier,
				     may_reserve, self->policy, &did_reserve,
				     &old_free);

		if (did_reserve) {
			treeF_t swap_free = old_free + frames;
			swap_reserved(self, request.tier, request.local,
				      tree_idx, swap_free);
		}
	} else {
		trees_put(&self->trees, tree_idx, frames, self->policy);
	}
	return llfree_ok(0, 0);
}

static treeF_t llfree_change_fetch_free(size_t idx, void *ctx)
{
	llfree_t *self = (llfree_t *)ctx;
	ll_stats_t stats =
		llfree_stats_at(self, frame_from_tree(idx), LLFREE_TREE_ORDER);
	return (treeF_t)stats.free_frames;
}

llfree_result_t llfree_change_tree(llfree_t *self, llfree_tree_match_t matcher,
				   llfree_tree_change_t change)
{
	assert(self != NULL);
	return trees_change(&self->trees, matcher, change,
			    llfree_change_fetch_free, self);
}

void llfree_drain(llfree_t *self)
{
	for (uint8_t t = 0; t < LLFREE_MAX_TIERS; t++) {
		size_t len = ll_local_tier_locals(self->local, t);
		if (len == LLFREE_LOCAL_NONE)
			continue;
		for (size_t i = 0; i < len; i++) {
			local_result_t old = ll_local_drain(self->local, t, i);
			if (!old.present)
				continue;
			trees_unreserve(&self->trees,
					tree_from_row(old.start_row), old.free,
					old.tier, self->policy);
		}
	}
}

size_t llfree_frames(const llfree_t *self)
{
	assert(self != NULL);
	return self->lower.frames;
}

ll_tree_stats_t llfree_tree_stats(const llfree_t *self)
{
	assert(self != NULL);
	ll_tree_stats_t stats = ll_local_stats(self->local);
	ll_tree_stats_t tree_stats = trees_stats(&self->trees);
	stats.free_frames += tree_stats.free_frames;
	stats.free_trees += tree_stats.free_trees;
	for (size_t t = 0; t < LLFREE_MAX_TIERS; t++) {
		stats.tiers[t].free_frames += tree_stats.tiers[t].free_frames;
		stats.tiers[t].alloc_frames += tree_stats.tiers[t].alloc_frames;
	}
	return stats;
}

ll_stats_t llfree_stats(const llfree_t *self)
{
	assert(self != NULL);
	return lower_stats(&self->lower);
}

ll_stats_t llfree_stats_at(const llfree_t *self, uint64_t frame, size_t order)
{
	assert(self != NULL);
	return lower_stats_at(&self->lower, frame, order);
}

void llfree_print_debug(const llfree_t *self,
			void (*writer)(void *, const char *), void *arg)
{
	assert(self != NULL);

	ll_tree_stats_t tree_stats = llfree_tree_stats(self);

	char msg[256];
	ll_stats_t stats = llfree_stats(self);
	size_t frames = llfree_frames(self);
	size_t huge = div_ceil(frames, 1 << LLFREE_HUGE_ORDER);
	snprintf(msg, sizeof(msg),
		 "LLC { tiers: %u, frames: %" PRIuS "/%" PRIuS ", huge: %" PRIuS
		 "/%" PRIuS ", trees: %" PRIuS "/%" PRIuS " }\n",
		 self->num_tiers, stats.free_frames, frames, stats.free_huge,
		 huge, tree_stats.free_trees, self->trees.len);
	writer(arg, msg);
}

void llfree_print(const llfree_t *self)
{
	llfree_info_start();
	llfree_info_cont("llfree_t {\n");
	ll_stats_t stats = llfree_stats(self);
	size_t frames = llfree_frames(self);
	size_t huge = div_ceil(frames, 1 << LLFREE_HUGE_ORDER);

	llfree_info_cont("%sframes: %" PRIuS ", huge: %" PRIuS "\n", INDENT(1),
			 frames, huge);

	ll_tree_stats_t tree_stats = llfree_tree_stats(self);
	llfree_info_cont("%sfree: { frames: %" PRIuS ", huge: %" PRIuS
			 ", trees: %" PRIuS " }\n",
			 INDENT(1), stats.free_frames, stats.free_huge,
			 tree_stats.free_trees);

	ll_local_print(self->local, 1);

	trees_print(&self->trees, 1);

	llfree_info_cont("}");
	llfree_info_end();
}

#define check(x)                               \
	({                                     \
		if (unlikely(!(x))) {          \
			llfree_warn("failed"); \
			assert(false);         \
		}                              \
	})

#define check_m(x, fmt, ...)                                        \
	({                                                          \
		if (unlikely((!(x)))) {                             \
			llfree_warn("failed: " fmt, ##__VA_ARGS__); \
			assert(false);                              \
		}                                                   \
	})

#define check_equal(fmt, actual, expected)                                   \
	({                                                                   \
		if (unlikely(((actual) != (expected)))) {                    \
			llfree_warn("failed: %%" fmt " == %%" fmt, (actual), \
				    (expected));                             \
			assert(false);                                       \
		}                                                            \
	})

static void validate_tree(const llfree_t *self, local_result_t res)
{
	assert(res.present);
	size_t tree_idx = tree_from_row(res.start_row);
	assert(tree_idx < self->trees.len);
	tree_t tree = trees_load(&self->trees, tree_idx);

	check(tree.reserved);
	check(tree.tier < self->num_tiers);
	treeF_t free = tree.free + res.free;
	check(free <= LLFREE_TREE_SIZE);
	ll_stats_t tree_stats = lower_stats_at(
		&self->lower, frame_from_row(res.start_row), LLFREE_TREE_ORDER);
	check_equal(PRIuS, (size_t)free, tree_stats.free_frames);
}

void llfree_validate(const llfree_t *self)
{
	ll_stats_t stats = lower_stats(&self->lower);
	ll_tree_stats_t fast_stats = llfree_tree_stats(self);
	check_equal(PRIuS, stats.free_frames, fast_stats.free_frames);

	for (size_t tree_idx = 0; tree_idx < self->trees.len; tree_idx++) {
		tree_t tree = trees_load(&self->trees, tree_idx);
		check(tree.free <= LLFREE_TREE_SIZE);
		check(tree.tier < self->num_tiers);
		if (!tree.reserved) {
			ll_stats_t tree_stats = lower_stats_at(
				&self->lower, frame_from_tree(tree_idx),
				LLFREE_TREE_ORDER);
			check_equal(PRIuS, tree_stats.free_frames,
				    (size_t)tree.free);
		}
	}

	ll_local_validate(self->local, self, validate_tree);
}
