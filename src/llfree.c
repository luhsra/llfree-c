#include "llfree_inner.h"

#include "llfree.h"
#include "child.h"
#include "llfree_platform.h"
#include "tree.h"
#include "local.h"
#include "lower.h"
#include "utils.h"

/// Initializes the Tree array by reading the child counters
static void init_trees(llfree_t *self)
{
	assert(self != NULL);

	for (size_t tree_idx = 0; tree_idx < self->trees_len; ++tree_idx) {
		treeF_t sum = 0;
		for (size_t child_idx = 0; child_idx < LLFREE_TREE_CHILDREN;
		     ++child_idx) {
			child_t child =
				atom_load(&self->lower.children[tree_idx]
						   .entries[child_idx]);
			sum += child.free;
		}
		// Entirely free trees use the default tier
		uint8_t tier = self->default_tier;
		self->trees[tree_idx] = tree_new(false, tier, sum);
	}
}

llfree_meta_size_t llfree_metadata_size(const llfree_tiering_t *tiering,
					size_t frames)
{
	size_t tree_len = div_ceil(frames, LLFREE_TREE_SIZE);
	size_t tree_size =
		align_up(sizeof(tree_t) * tree_len, LLFREE_CACHE_SIZE);
	llfree_meta_size_t meta = {
		.llfree = sizeof(llfree_t),
		.trees = tree_size,
		.local = ll_local_size(tiering),
		.lower = lower_metadata_size(frames),
	};
	return meta;
}

llfree_meta_size_t llfree_metadata_size_of(const llfree_t *self)
{
	assert(self != NULL);
	size_t tree_size =
		align_up(sizeof(tree_t) * self->trees_len, LLFREE_CACHE_SIZE);
	return (llfree_meta_size_t){
		.llfree = sizeof(llfree_t),
		.trees = tree_size,
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

	self->trees_len = div_ceil(frames, LLFREE_TREE_SIZE);
	self->trees = (_Atomic(tree_t) *)(meta.trees);
	self->local = (local_t *)meta.local;
	ll_local_init(self->local, tiering);

	self->policy = tiering->policy;
	self->default_tier = tiering->default_tier;
	self->num_tiers = (uint8_t)tiering->num_tiers;

	if (init != LLFREE_INIT_NONE)
		init_trees(self);
	return llfree_ok(0, 0);
}

llfree_meta_t llfree_metadata(const llfree_t *self)
{
	assert(self != NULL);
	llfree_meta_t meta = {
		.local = (uint8_t *)self->local,
		.trees = (uint8_t *)self->trees,
		.lower = lower_metadata(&self->lower),
	};
	return meta;
}

/// Search function callback
typedef llfree_result_t (*search_fn)(llfree_t *self, size_t idx, void *args);

/// Searches the tree array for a tree and executes the callback for it
/// until it stops returning ERR_MEMORY.
static llfree_result_t search(llfree_t *self, uint64_t start, uint64_t offset,
			      uint64_t len, search_fn callback, void *args)
{
	int64_t base = (int64_t)(start + self->trees_len);
	for (int64_t i = (int64_t)offset; i < (int64_t)len; ++i) {
		// Search alternating left and right from start
		int64_t off = i % 2 == 0 ? i / 2 : -((i + 1) / 2);
		uint64_t idx = (uint64_t)(base + off) % self->trees_len;
		llfree_result_t res = callback(self, idx, args);

		if (res.error != LLFREE_ERR_MEMORY) {
			if (i - offset > 4)
				llfree_debug(
					"large global search o=%" PRIu64
					" i=%" PRIu64 ": %" PRIu64 " -> %d",
					offset, i, len,
					llfree_is_ok(res) ?
						tree_from_frame(res.frame) :
						-1);
			return res;
		}
	}
	if (len > 4)
		llfree_debug("large global search failed o=%" PRIu64
			     " i=%" PRIu64 ": %" PRIu64,
			     offset, len, start + len);

	return llfree_err(LLFREE_ERR_MEMORY);
}

typedef size_t (*prio_fn)(uint8_t tier, uint8_t order, tree_t tree, void *args);

#define SEARCH_BEST 3
__attribute((unused)) static llfree_result_t
search_best(llfree_t *self, uint8_t tier, uint8_t order, uint64_t start,
	    uint64_t offset, uint64_t len, prio_fn tree_prio_fn,
	    search_fn callback, void *args)
{
	assert(self != NULL && callback != NULL);

	struct best {
		size_t prio; // present if > 0
		uint64_t idx;
	};
	struct best best[SEARCH_BEST] = { 0 };
	for (size_t i = 0; i < SEARCH_BEST; ++i)
		best[i].prio = 0;

	for (int64_t i = (int64_t)offset; i < (int64_t)len; ++i) {
		// Search alternating left and right from start
		int64_t off = i % 2 == 0 ? i / 2 : -((i + 1) / 2);
		int64_t base = (int64_t)(start + self->trees_len);
		uint64_t idx = (uint64_t)(base + off) % self->trees_len;

		tree_t tree = atom_load(&self->trees[idx]);
		size_t prio = (tree_prio_fn)(tier, order, tree, args);
		if (prio == 0)
			continue;

		// Use highest prio directly
		if (prio >= 100) {
			llfree_result_t res = callback(self, idx, args);
			if (res.error != LLFREE_ERR_MEMORY)
				return res;
			continue;
		}

		size_t pos = 0;
		for (; pos < SEARCH_BEST; ++pos) {
			// Only replace if better -> prefer trees near the start
			if (prio > best[pos].prio)
				break;
		}
		if (pos < SEARCH_BEST) {
			for (size_t j = pos; j < SEARCH_BEST - 1; ++j) {
				best[j + 1] = best[j];
			}
			best[pos].prio = prio;
			best[pos].idx = idx;
		}
	}
	// Try best candidates
	for (size_t i = 0; i < SEARCH_BEST; ++i) {
		if (best[i].prio == 0)
			break;
		llfree_result_t res = callback(self, best[i].idx, args);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;
	}

	return llfree_err(LLFREE_ERR_MEMORY);
}

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
		tree_t tree;
		// Return old frames to global; policy may adjust tier
		atom_update(&self->trees[tree_from_row(old.start_row)], tree,
			    tree_unreserve_add, old.free, old.tier,
			    self->policy, self->default_tier);
	}
}

typedef struct reserve_args {
	uint8_t tier;
	size_t index;
	uint8_t order;
	p_range_t free;
} reserve_args_t;

/// Reserves a new tree and allocates from it.
/// Only if the allocation succeeds, the tree is fully reserved.
static llfree_result_t reserve_tree_and_get(llfree_t *self, size_t idx,
					    void *args)
{
	assert(idx < self->trees_len);

	reserve_args_t *rargs = (reserve_args_t *)args;
	uint8_t tier = rargs->tier;
	treeF_t needed = (treeF_t)(1u << rargs->order);
	treeF_t max = rargs->free.max;
	treeF_t min = LL_MAX(rargs->free.min, needed);
	if (max < min)
		return llfree_err(LLFREE_ERR_MEMORY);

	tree_t old;
	if (!atom_update(&self->trees[idx], old, tree_reserve, tier, min, max))
		return llfree_err(LLFREE_ERR_MEMORY);
	assert(!old.reserved && old.free >= min);

	llfree_result_t res = lower_get(&self->lower, frame_from_tree(idx),
					rargs->order, ll_none());

	if (llfree_is_ok(res)) {
		// old.free moved into local reservation; subtract what we used
		treeF_t new_free = old.free - needed;
		swap_reserved(self, rargs->tier, rargs->index, idx, new_free);
		res.tier = tier; // set actual tier in result
	} else {
		llfree_debug("reserve failed tier=%u index=%zu idx=%zu",
			     rargs->tier, rargs->index, idx);
		// undo: restore free counter and unreserve
		tree_t tree;
		atom_update(&self->trees[idx], tree, tree_unreserve_add,
			    old.free, old.tier, self->policy,
			    self->default_tier);
	}
	return res;
}

static inline size_t tree_prio(uint8_t tier, uint8_t order, tree_t tree,
			       void *args)
{
	(void)args;
	if (tree.free == 0)
		return 0; // entirely allocated
	if (tree.free == LLFREE_TREE_SIZE)
		return 0; // entirely free (avoid fragmenting empty trees)

	if (tree.tier != tier)
		return 0; // wrong tier

	// huge tier: perfect match
	if (order >= LLFREE_HUGE_ORDER)
		return 100;

	if (tree.free >= LLFREE_TREE_SIZE / 2)
		return 100;

	return 2; // fallback
}

/// Reserves and allocates from a new tree, searching near `start`.
static llfree_result_t reserve_and_get(llfree_t *self, uint8_t tier,
				       size_t index, uint8_t order,
				       uint64_t start)
{
	assert(start < self->trees_len);

	llfree_result_t res;
	const size_t cl_trees = LLFREE_CACHE_SIZE / sizeof(tree_t);
	size_t near = LL_MAX(self->trees_len / 16, cl_trees / 4);
	start = align_down(start, next_pow2(2 * near));

	reserve_args_t args = {
		.tier = tier, .index = index, .order = order, .free = { 0, 0 }
	};

	llfree_debug("reserve tier=%u index=%zu o=%d", tier, index, order);

	if (order < LLFREE_HUGE_ORDER) {
		// Avoid fragmenting fully free trees
		args.free = (p_range_t){ 0, LLFREE_TREE_SIZE - 1 };

		res = search_best(self, tier, order, start, 1, 2 * near,
				  tree_prio, reserve_tree_and_get, &args);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;

		res = search(self, start, 1, self->trees_len,
			     reserve_tree_and_get, &args);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;
	}

	// Any tree (including fully free ones)
	args.free = (p_range_t){ 0, LLFREE_TREE_SIZE };
	res = search(self, start, 0, self->trees_len, reserve_tree_and_get,
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

	tree_t old_tree;
	if (!atom_update(&self->trees[tree_idx], old_tree, tree_sync_steal,
			 steal_min))
		return false;

	// old_tree.free = frames that were available (now stolen from global)
	if (!ll_local_put(self->local, tier, index, tree_idx, old_tree.free)) {
		llfree_warn("sync local failed tier=%u index=%zu idx=%zu", tier,
			    index, tree_idx);
		// Revert the steal
		tree_t t;
		atom_update(&self->trees[tree_idx], t, tree_put, old_tree.free,
			    self->default_tier);
		return false;
	}
	llfree_debug("sync success tier=%u index=%zu free=%" PRIuS, tier, index,
		     (size_t)old_tree.free);
	return true;
}

static llfree_result_t get_from_local(llfree_t *self, uint8_t tier,
				      size_t index, uint8_t order,
				      treeF_t frames, local_result_t *old)
{
	// Try decrementing the local counter
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

		// Undo decrement: put frames back to global tree
		tree_t tree;
		atom_update(&self->trees[tree_from_row(old->start_row)], tree,
			    tree_put, frames, self->default_tier);
		return res;
	}

	// Try synchronizing with global counter
	if (old->present && sync_with_global(self, tier, index, frames, *old)) {
		return llfree_err(LLFREE_ERR_RETRY);
	}

	return llfree_err(LLFREE_ERR_MEMORY);
}

/// Allocate from a global tree, decrementing the tree counter.
/// Only allows Policy::Match (no demotion). Matches Rust get_matching_global.
typedef struct match_global_args {
	uint8_t tier;
	uint8_t order;
	llfree_policy_fn policy;
} match_global_args_t;

static llfree_result_t get_global_match(llfree_t *self, size_t idx, void *args)
{
	assert(idx < self->trees_len);

	match_global_args_t *rargs = args;
	treeF_t frames = (treeF_t)(1u << rargs->order);

	tree_t old;
	if (!atom_update(&self->trees[idx], old, tree_get_match, rargs->tier,
			 frames, rargs->policy))
		return llfree_err(LLFREE_ERR_MEMORY);

	llfree_result_t res = lower_get(&self->lower, frame_from_tree(idx),
					rargs->order, ll_none());
	if (!llfree_is_ok(res)) {
		tree_t t;
		atom_update(&self->trees[idx], t, tree_put, frames,
			    self->default_tier);
		return res;
	}
	return llfree_ok(res.frame, old.tier);
}

/// Reserve a tree with Demote-only policy and allocate from it.
/// Matches Rust get_demoting's global reserve path.
typedef struct reserve_demote_args {
	uint8_t tier;
	size_t index;
	uint8_t order;
	llfree_policy_fn policy;
} reserve_demote_args_t;

static llfree_result_t reserve_tree_demote_and_get(llfree_t *self, size_t idx,
						   void *args)
{
	assert(idx < self->trees_len);

	reserve_demote_args_t *rargs = (reserve_demote_args_t *)args;
	uint8_t tier = rargs->tier;
	treeF_t needed = (treeF_t)(1u << rargs->order);

	tree_t old;
	if (!atom_update(&self->trees[idx], old, tree_reserve_demote, tier,
			 needed, rargs->policy))
		return llfree_err(LLFREE_ERR_MEMORY);
	assert(!old.reserved && old.free >= needed);

	llfree_result_t res = lower_get(&self->lower, frame_from_tree(idx),
					rargs->order, ll_none());

	if (llfree_is_ok(res)) {
		treeF_t new_free = old.free - needed;
		swap_reserved(self, rargs->tier, rargs->index, idx, new_free);
		res.tier = tier;
	} else {
		tree_t tree;
		atom_update(&self->trees[idx], tree, tree_unreserve_add,
			    old.free, old.tier, self->policy,
			    self->default_tier);
	}
	return res;
}

/// Demote-only global search: search for a tree and decrement its counter,
/// only if policy returns Demote.
typedef struct demote_only_global_args {
	uint8_t tier;
	uint8_t order;
	llfree_policy_fn policy;
} demote_only_global_args_t;

static llfree_result_t get_global_demote_only(llfree_t *self, size_t idx,
					      void *args)
{
	assert(idx < self->trees_len);

	demote_only_global_args_t *rargs = args;
	treeF_t frames = (treeF_t)(1u << rargs->order);

	tree_t old;
	if (!atom_update(&self->trees[idx], old, tree_get_demote_only,
			 rargs->tier, frames, rargs->policy))
		return llfree_err(LLFREE_ERR_MEMORY);

	llfree_result_t res = lower_get(&self->lower, frame_from_tree(idx),
					rargs->order, ll_none());
	if (!llfree_is_ok(res)) {
		tree_t t;
		atom_update(&self->trees[idx], t, tree_put, frames,
			    self->default_tier);
		return res;
	}
	return llfree_ok(res.frame, rargs->tier);
}

/// Steal from any local reservation (matches Rust steal_local).
/// Tries Match/Steal policy only — no demotion.
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
		// Undo decrement on failure
		tree_t tree;
		atom_update(&self->trees[tree_from_row(res.start_row)], tree,
			    tree_put, frames, self->default_tier);
		if (res2.error != LLFREE_ERR_MEMORY)
			return res2;
	}

	return llfree_err(LLFREE_ERR_MEMORY);
}

/// Demote from a local reservation (matches Rust demote_local).
/// Finds a target in another tier where policy returns Demote, takes its tree,
/// swaps it into the requesting local, unreserves the old requesting local.
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
		// Unreserve the old tree from the requesting local
		if (dem.old_present) {
			tree_t tree;
			atom_update(&self->trees[tree_from_row(dem.old_row)],
				    tree, tree_unreserve_add, dem.old_free,
				    dem.old_tier, self->policy,
				    self->default_tier);
		}

		ll_optional_t lower_frame = frame;
		llfree_result_t res = lower_get(&self->lower,
						frame_from_row(dem.row),
						request->order, lower_frame);
		if (llfree_is_ok(res))
			return llfree_ok(res.frame, request->tier);
		// Undo counter decrement
		tree_t tree;
		atom_update(&self->trees[tree_from_row(dem.row)], tree,
			    tree_put, frames, self->default_tier);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;
	}
	return llfree_err(LLFREE_ERR_MEMORY);
}

/// Try matching-only allocation: local retry + reserve (matches Rust get_matching).
static llfree_result_t
get_matching(llfree_t *self, const llfree_request_t *request, uint64_t *start)
{
	size_t tier_count = ll_local_tier_locals(self->local, request->tier);
	treeF_t frames = (treeF_t)(1u << request->order);

	if (request->local != LLFREE_LOCAL_NONE && tier_count > 0 &&
	    tier_count < self->trees_len) {
		// Local path: retry local + reserve
		for (size_t i = 0; i < RETRIES; i++) {
			local_result_t old;
			llfree_result_t res = get_from_local(
				self, request->tier, request->local,
				request->order, frames, &old);
			if (llfree_is_ok(res))
				return res;
			if (res.error == LLFREE_ERR_RETRY)
				continue;

			// Update start hint from current reservation
			if (old.present)
				*start = tree_from_row(old.start_row);

			// Try out of retry loop into reserve
			break;
		}
		// Reserve a new tree (matching tier only)
		return reserve_and_get(self, request->tier, request->local,
				       request->order, *start);
	} else {
		// Global path: match only, no demotion
		match_global_args_t args = { .tier = request->tier,
					     .order = request->order,
					     .policy = self->policy };
		return search(self, *start, 0, self->trees_len,
			      get_global_match, &args);
	}
}

/// Allocate with demotion (matches Rust get_demoting).
/// Tries global reserve with demotion, then demote_local.
static llfree_result_t
get_demoting(llfree_t *self, const llfree_request_t *request, uint64_t *start)
{
	size_t tier_count = ll_local_tier_locals(self->local, request->tier);

	if (request->local != LLFREE_LOCAL_NONE && tier_count > 0 &&
	    tier_count < self->trees_len) {
		// Try reserve a new tree with demotion
		reserve_demote_args_t args = { .tier = request->tier,
					       .index = request->local,
					       .order = request->order,
					       .policy = self->policy };
		llfree_result_t res = search(self, *start, 0, self->trees_len,
					     reserve_tree_demote_and_get,
					     &args);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;

		// Then try demoting another local
		return demote_local(self, request, ll_none());
	} else {
		// Global path with demotion-only
		demote_only_global_args_t args = { .tier = request->tier,
						   .order = request->order,
						   .policy = self->policy };
		return search(self, *start, 0, self->trees_len,
			      get_global_demote_only, &args);
	}
}

static llfree_result_t llfree_get_at(llfree_t *self, size_t frame,
				     llfree_request_t request)
{
	// Fixed-frame allocation: allocate a specific frame
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
				tree_t tree;
				atom_update(&self->trees[tree_idx], tree,
					    tree_put, frames,
					    self->default_tier);
				return res;
			}
			// Sync with global if present but insufficient
			if (old.present &&
			    sync_with_global(self, request.tier, request.local,
					     frames, old))
				continue;
			break;
		}
	}

	// Fallback to global (with all policies)
	tree_t old_t;
	if (atom_update(&self->trees[tree_idx], old_t, tree_get_demote,
			request.tier, frames, self->policy)) {
		llfree_policy_t p =
			self->policy(request.tier, old_t.tier, old_t.free);
		uint8_t result_tier = (p.type == LLFREE_POLICY_DEMOTE) ?
					      request.tier :
					      old_t.tier;
		llfree_result_t res = lower_get(&self->lower, frame,
						request.order, ll_some(frame));
		if (llfree_is_ok(res))
			return llfree_ok(res.frame, result_tier);
		tree_t tree;
		atom_update(&self->trees[tree_idx], tree, tree_put, frames,
			    self->default_tier);
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

	// Compute start_idx: spread locals evenly across trees
	size_t tier_count = ll_local_tier_locals(self->local, request.tier);
	uint64_t start;
	if (tier_count > 0 && request.local != LLFREE_LOCAL_NONE) {
		start = self->trees_len / tier_count * request.local;
	} else {
		start = 0;
	}

	// Try local + reserve, matching tier only
	llfree_result_t res = get_matching(self, &request, &start);
	if (res.error != LLFREE_ERR_MEMORY)
		return res;

	// Try reserve with demotion + demote_local
	res = get_demoting(self, &request, &start);
	if (res.error != LLFREE_ERR_MEMORY)
		return res;

	// OOM: downgrade tier and retry get_matching
	llfree_warn("OOM");
	for (uint8_t t = 1; t < LLFREE_MAX_TIERS; t++) {
		uint8_t down_tier =
			(uint8_t)((LLFREE_MAX_TIERS + request.tier - t) %
				  LLFREE_MAX_TIERS);
		if (ll_local_tier_locals(self->local, down_tier) > 0) {
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

	// Try stealing from any local reservation
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
		// Update free-reserve heuristic
		bool reserve = LLFREE_ENABLE_FREE_RESERVE &&
			       ll_local_free_inc(self->local, request.tier,
						 request.local, tree_idx);

		tree_t old;

		// Increment local reservation if tree matches
		if (ll_local_put(self->local, request.tier, request.local,
				 tree_idx, frames)) {
			reserve = false;
		} else {
			// Update global tree or reserve for next time
			atom_update(&self->trees[tree_idx], old,
				    tree_put_or_reserve, frames, tier, &reserve,
				    TREE_LOWER_LIM, self->default_tier);
		}

		// Free-reserve heuristic: reserve trees with lots of frees
		if (reserve) {
			tree_t new = old;
			bool success =
				tree_put(&new, frames, self->default_tier);
			assert(success);
			new.reserved = true;
			swap_reserved(self, request.tier, request.local,
				      tree_idx, new.free);
		}
	} else {
		// No local slot: update global tree directly
		tree_t t;
		atom_update(&self->trees[tree_idx], t, tree_put, frames,
			    self->default_tier);
	}
	return llfree_ok(0, 0);
}

void llfree_drain(llfree_t *self)
{
	for (uint8_t t = 0; t < LLFREE_MAX_TIERS; t++) {
		size_t len = ll_local_tier_locals(self->local, t);
		for (size_t i = 0; i < len; i++) {
			local_result_t old = ll_local_drain(self->local, t, i);
			if (!old.present)
				continue;
			tree_t tree;
			atom_update(&self->trees[tree_from_row(old.start_row)],
				    tree, tree_unreserve_add, old.free,
				    old.tier, self->policy, self->default_tier);
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

	// Local counters
	ll_tree_stats_t stats = ll_local_stats(self->local);

	// Global counters
	for (size_t i = 0; i < self->trees_len; i++) {
		tree_t t = atom_load(&self->trees[i]);
		stats.free_frames += t.free;
		stats.free_trees += t.free == LLFREE_TREE_SIZE;

		ll_tier_stats_t *tier = &stats.tiers[t.tier];
		tier->free_frames += t.free;
		tier->alloc_frames += LLFREE_TREE_SIZE - t.free;
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
		 huge, tree_stats.free_trees, self->trees_len);
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

	llfree_info_cont("%strees: %" PRIuS " (%u) {\n", INDENT(1),
			 self->trees_len, LLFREE_TREE_SIZE);

	for (size_t i = 0; i < self->trees_len; i++) {
		tree_t tree = atom_load(&self->trees[i]);
		tree_print(&tree, i, 2);
	}
	llfree_info_cont("%s}\n", INDENT(1));

	llfree_info_cont("}");
	llfree_info_end();
}

#define check(x)                               \
	({                                     \
		if (!(x)) {                    \
			llfree_warn("failed"); \
			assert(false);         \
		}                              \
	})

#define check_m(x, fmt, ...)                                        \
	({                                                          \
		if (!(x)) {                                         \
			llfree_warn("failed: " fmt, ##__VA_ARGS__); \
			assert(false);                              \
		}                                                   \
	})

#define check_equal(fmt, actual, expected)                                 \
	({                                                                 \
		if ((actual) != (expected)) {                              \
			llfree_warn("failed: %" fmt " == %" fmt, (actual), \
				    (expected));                           \
			assert(false);                                     \
		}                                                          \
	})

static void validate_tree(const llfree_t *self, local_result_t res)
{
	assert(res.present);
	size_t tree_idx = tree_from_row(res.start_row);
	assert(tree_idx < self->trees_len);
	tree_t tree = atom_load(&self->trees[tree_idx]);

	check(tree.reserved);
	check(tree.tier < self->num_tiers);
	treeF_t free = tree.free + res.free;
	check(free <= LLFREE_TREE_SIZE);
	if (tree.tier == self->num_tiers - 1) {
		check(res.free % (1u << LLFREE_HUGE_ORDER) == 0);
		check(tree.free % (1u << LLFREE_HUGE_ORDER) == 0);
	}
	ll_stats_t tree_stats = lower_stats_at(
		&self->lower, frame_from_row(res.start_row), LLFREE_TREE_ORDER);
	check_equal(PRIuS, (size_t)free, tree_stats.free_frames);
}

void llfree_validate(const llfree_t *self)
{
	ll_stats_t stats = lower_stats(&self->lower);
	ll_tree_stats_t fast_stats = llfree_tree_stats(self);
	check_equal(PRIuS, stats.free_frames, fast_stats.free_frames);

	for (size_t tree_idx = 0; tree_idx < self->trees_len; tree_idx++) {
		tree_t tree = atom_load(&self->trees[tree_idx]);
		check(tree.free <= LLFREE_TREE_SIZE);
		check(tree.tier < self->num_tiers);
		if (!tree.reserved) {
			ll_stats_t tree_stats = lower_stats_at(
				&self->lower, frame_from_tree(tree_idx),
				LLFREE_TREE_ORDER);
			check_equal(PRIuS, tree_stats.free_frames,
				    (size_t)tree.free);

			if (tree.tier == self->num_tiers - 1) {
				check(tree.free % (1u << LLFREE_HUGE_ORDER) ==
				      0);
				check_equal(PRIuS, tree_stats.free_huge,
					    (size_t)tree.free >>
						    LLFREE_HUGE_ORDER);
			}
		}
	}

	ll_local_validate(self->local, self, validate_tree);
}
