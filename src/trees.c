#include "trees.h"
#include "llfree_platform.h"
#include "tree.h"
#include "utils.h"

void trees_init(trees_t *self, size_t frames, uint8_t *buffer,
		trees_init_fn init_fn, void *init_ctx, uint8_t default_tier)
{
	self->len = div_ceil(frames, LLFREE_TREE_SIZE);
	self->entries = (_Atomic(tree_t) *)buffer;
	self->default_tier = default_tier;

	if (init_fn != NULL) {
		for (size_t i = 0; i < self->len; ++i) {
			treeF_t free = init_fn(i * LLFREE_TREE_SIZE, init_ctx);
			self->entries[i] = tree_new(false, default_tier, free);
		}
	}
}

uint8_t *trees_metadata(const trees_t *self)
{
	return (uint8_t *)self->entries;
}

tree_t trees_load(const trees_t *self, size_t idx)
{
	assert(idx < self->len);
	return atom_load(&self->entries[idx]);
}

bool trees_get(trees_t *self, size_t idx, treeF_t frames,
	       tree_check_fn check, void *args, uint8_t *out_tier)
{
	assert(idx < self->len);
	tree_t old;
	bool ok = atom_update(&self->entries[idx], old, tree_get, frames,
			      out_tier, check, args);
	return ok;
}

void trees_put(trees_t *self, size_t idx, treeF_t frames,
	       llfree_policy_fn policy)
{
	assert(idx < self->len);
	(void)policy; // reserved for future use (see Rust tree_put policy check)
	tree_t old;
	atom_update(&self->entries[idx], old, tree_put, frames,
		    self->default_tier);
}

bool trees_reserve(trees_t *self, size_t idx, tree_check_fn check, void *args,
		   treeF_t *out_free, uint8_t *out_tier)
{
	assert(idx < self->len);
	tree_t old;
	bool ok = atom_update(&self->entries[idx], old, tree_reserve, out_tier,
			      check, args);
	if (ok && out_free != NULL)
		*out_free = old.free;
	return ok;
}

void trees_unreserve(trees_t *self, size_t idx, treeF_t free, uint8_t tier,
		     llfree_policy_fn policy)
{
	assert(idx < self->len);
	tree_t old;
	atom_update(&self->entries[idx], old, tree_unreserve_add, free, tier,
		    policy, self->default_tier);
}

bool trees_sync_steal(trees_t *self, size_t idx, treeF_t min,
		      treeF_t *out_stolen)
{
	assert(idx < self->len);
	tree_t old;
	bool ok = atom_update(&self->entries[idx], old, tree_sync_steal, min);
	if (ok && out_stolen != NULL)
		*out_stolen = old.free;
	return ok;
}

void trees_put_or_reserve(trees_t *self, size_t idx, treeF_t frames,
			  uint8_t tier, bool may_reserve,
			  llfree_policy_fn policy, bool *did_reserve,
			  treeF_t *out_old_free)
{
	assert(idx < self->len);
	(void)policy;
	// Manual CAS loop: may_reserve must stay constant across retries
	// (unlike the old tree_put_or_reserve which modified *reserve in place)
	tree_t old = atom_load(&self->entries[idx]);
	while (true) {
		tree_t new = old;
		new.free += frames;
		assert(new.free <= LLFREE_TREE_SIZE);
		if (new.free == LLFREE_TREE_SIZE)
			new.tier = self->default_tier;

		bool reserve_this = may_reserve && !new.reserved &&
				    new.tier == tier &&
				    new.free > TREES_MIN_FREE;
		if (reserve_this) {
			new.free = 0;
			new.reserved = true;
		}

		if (atom_cmp_exchange_weak(&self->entries[idx], &old, new)) {
			*did_reserve = reserve_this;
			if (reserve_this && out_old_free != NULL)
				*out_old_free = old.free;
			return;
		}
	}
}

llfree_result_t trees_search(const trees_t *self, size_t start, size_t offset,
			     size_t len, trees_access_fn cb, void *ctx)
{
	int64_t base = (int64_t)(start + self->len);
	for (int64_t i = (int64_t)offset; i < (int64_t)len; ++i) {
		int64_t off = i % 2 == 0 ? i / 2 : -((i + 1) / 2);
		size_t idx = (size_t)(base + off) % self->len;
		llfree_result_t res = cb(idx, ctx);
		if (res.error != LLFREE_ERR_MEMORY) {
			if (i - (int64_t)offset > 4)
				llfree_debug(
					"large search o=%zu i=%zd len=%zu -> tree %zu",
					offset, (ssize_t)i, len,
					llfree_is_ok(res) ?
						tree_from_frame(res.frame) :
						(size_t)-1);
			return res;
		}
	}
	if (len > 4)
		llfree_debug("search failed o=%zu len=%zu start=%zu", offset,
			     len, start);
	return llfree_err(LLFREE_ERR_MEMORY);
}

llfree_result_t trees_search_best(const trees_t *self, uint8_t tier,
				  size_t start, size_t offset, size_t len,
				  treeF_t min_free, llfree_policy_fn policy,
				  trees_access_fn cb, void *ctx)
{
	struct best {
		uint8_t prio; // present if > 0
		size_t idx;
	};
	struct best best[TREES_SEARCH_BEST] = { 0 };

	int64_t base = (int64_t)(start + self->len);
	for (int64_t i = (int64_t)offset; i < (int64_t)len; ++i) {
		int64_t off = i % 2 == 0 ? i / 2 : -((i + 1) / 2);
		size_t idx = (size_t)(base + off) % self->len;

		tree_t tree = atom_load(&self->entries[idx]);
		if (tree.reserved || tree.free < min_free ||
		    tree.free == LLFREE_TREE_SIZE)
			continue;

		llfree_policy_t p = policy(tier, tree.tier, tree.free);
		if (p.type != LLFREE_POLICY_MATCH)
			continue;

		// Perfect match: try immediately
		if (p.priority == UINT8_MAX) {
			llfree_result_t res = cb(idx, ctx);
			if (res.error != LLFREE_ERR_MEMORY)
				return res;
			continue;
		}

		// Priority+1 so 0 means "no candidate"
		uint8_t prio = p.priority + 1;

		size_t pos = 0;
		for (; pos < TREES_SEARCH_BEST; ++pos) {
			if (prio > best[pos].prio)
				break;
		}
		if (pos < TREES_SEARCH_BEST) {
			for (size_t j = TREES_SEARCH_BEST - 1; j > pos; --j)
				best[j] = best[j - 1];
			best[pos].prio = prio;
			best[pos].idx = idx;
		}
	}

	for (size_t i = 0; i < TREES_SEARCH_BEST; ++i) {
		if (best[i].prio == 0)
			break;
		llfree_result_t res = cb(best[i].idx, ctx);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;
	}

	return llfree_err(LLFREE_ERR_MEMORY);
}

ll_tree_stats_t trees_stats(const trees_t *self)
{
	ll_tree_stats_t stats = { 0 };
	for (size_t i = 0; i < self->len; i++) {
		tree_t t = atom_load(&self->entries[i]);
		stats.free_frames += t.free;
		stats.free_trees += t.free == LLFREE_TREE_SIZE;

		ll_tier_stats_t *tier = &stats.tiers[t.tier];
		tier->free_frames += t.free;
		tier->alloc_frames += LLFREE_TREE_SIZE - t.free;
	}
	return stats;
}

void trees_stats_at(const trees_t *self, size_t idx, uint8_t *tier,
		    treeF_t *free, bool *reserved)
{
	assert(idx < self->len);
	tree_t t = atom_load(&self->entries[idx]);
	if (tier != NULL)
		*tier = t.tier;
	if (free != NULL)
		*free = t.free;
	if (reserved != NULL)
		*reserved = t.reserved;
}

typedef struct change_at_args {
	trees_t *trees;
	llfree_tree_match_t matcher;
	llfree_tree_change_t change;
	trees_fetch_free_fn fetch_free;
	void *fetch_ctx;
} change_at_args_t;

static llfree_result_t trees_change_at(size_t idx, void *ctx)
{
	change_at_args_t *args = (change_at_args_t *)ctx;
	tree_t old = atom_load(&args->trees->entries[idx]);

	while (true) {
		treeF_t online_free = 0;
		if (args->change.operation == LLFREE_TREE_OP_ONLINE) {
			online_free = args->fetch_free(idx, args->fetch_ctx);
		}

		tree_t desired = old;
		if (!tree_change(&desired, args->matcher.tier,
				 (treeF_t)args->matcher.free,
				 args->change.tier,
				 args->change.operation,
				 online_free)) {
			return llfree_err(LLFREE_ERR_MEMORY);
		}

		if (atom_cmp_exchange_weak(&args->trees->entries[idx], &old,
					   desired)) {
			return llfree_ok(0, 0);
		}
	}
}

llfree_result_t trees_change(trees_t *self, llfree_tree_match_t matcher,
			     llfree_tree_change_t change,
			     trees_fetch_free_fn fetch_free, void *fetch_ctx)
{
	if (matcher.free > LLFREE_TREE_SIZE)
		return llfree_err(LLFREE_ERR_MEMORY);

	change_at_args_t args = {
		.trees = self,
		.matcher = matcher,
		.change = change,
		.fetch_free = fetch_free,
		.fetch_ctx = fetch_ctx,
	};

	if (matcher.id.present) {
		if (matcher.id.value >= self->len)
			return llfree_err(LLFREE_ERR_MEMORY);
		return trees_change_at(matcher.id.value, &args);
	}

	return trees_search(self, 0, 0, self->len, trees_change_at, &args);
}

void trees_print(const trees_t *self, size_t indent)
{
	llfree_info_cont("%strees: %zu (%u) {\n", INDENT(indent), self->len,
			 LLFREE_TREE_SIZE);
	for (size_t i = 0; i < self->len; i++) {
		tree_t tree = atom_load(&self->entries[i]);
		tree_print(&tree, i, indent + 1);
	}
	llfree_info_cont("%s}\n", INDENT(indent));
}
