#include "local.h"
#include "llfree.h"
#include "tree.h"
#include "utils.h"

/// CPU-local data of the currently reserved tree
typedef struct reserved {
	/// true if there is a reserved tree
	bool present : 1;
	/// Number of free frames in the tree
	treeF_t free : LLFREE_TREE_FREE_BITS;
	/// Bitfield row index of reserved tree,
	/// used for identifying the reserved tree and as starting point
	/// for the next allocation
	uint64_t start_row : 64 - LLFREE_TREE_FREE_BITS - 1;
} reserved_t;
_Static_assert(sizeof(reserved_t) == sizeof(uint64_t), "size overflow");

#if LLFREE_ENABLE_FREE_RESERVE
/// Counts last frees in same tree
typedef struct local_history {
	/// Index of the last tree where a frame was freed
	uint64_t idx : 48;
	/// Number of consecutive frees in the same tree
	uint16_t frees : 16;
} local_history_t;
_Static_assert(sizeof(local_history_t) == sizeof(uint64_t), "size overflow");
#endif // LLFREE_ENABLE_FREE_RESERVE

static inline reserved_t ll_reserved_new(bool present, treeF_t free,
					 row_id_t start_row)
{
	assert(free <= LLFREE_TREE_SIZE);
	return (reserved_t){ present, free, start_row.value };
}

static bool ll_reserved_dec(reserved_t *self, tree_id_optional_t tree_idx,
			    treeF_t frames)
{
	if (!self->present)
		return false;
	if (tree_idx.present &&
	    tree_from_row(row_id(self->start_row)).value !=
		    tree_idx.value.value)
		return false;
	if (self->free < frames)
		return false;
	self->free -= frames;
	return true;
}

static bool ll_reserved_inc(reserved_t *self, tree_id_t tree_idx,
			    treeF_t frames)
{
	if (!self->present ||
	    tree_from_row(row_id(self->start_row)).value != tree_idx.value)
		return false;
	treeF_t free = self->free + frames;
	assert(free <= LLFREE_TREE_SIZE);
	self->free = free;
	return true;
}

static bool ll_reserved_swap(reserved_t *self, reserved_t new)
{
	*self = new;
	return true;
}

static bool ll_reserved_set_start(reserved_t *self, row_id_t start_row)
{
	if (!self->present ||
	    tree_from_row(row_id(self->start_row)).value !=
		    tree_from_row(start_row).value)
		return false;
	self->start_row = start_row.value;
	return true;
}

/// Atomically take a present reservation (clears it)
static bool ll_reserved_take(reserved_t *self)
{
	if (!self->present)
		return false;
	*self = ll_reserved_new(false, 0, row_id(0));
	return true;
}

// ----------------------------------------------------------------------------
//
// Local CPU data
//
// ----------------------------------------------------------------------------

/// One local entry per core per tier
typedef struct __attribute__((aligned(LLFREE_CACHE_SIZE))) entry {
	/// Currently reserved tree for this slot
	_Atomic(reserved_t) preferred;
#if LLFREE_ENABLE_FREE_RESERVE
	/// Counts recent frees to the same tree (heuristic for reserving)
	_Atomic(local_history_t) last;
#endif
} entry_t;
_Static_assert(sizeof(entry_t) == LLFREE_CACHE_SIZE,
	       "entry_t exceeds cache line");

/// Slice of entries for one tier
typedef struct tier_locals {
	entry_t *entries; // NULL if no locals for this tier
	size_t len; // LLFREE_LOCAL_NONE if tier not configured
} tier_locals_t;

/// Locals struct
typedef struct __attribute__((aligned(LLFREE_CACHE_SIZE))) local {
	/// Number of tiers
	uint8_t num_tiers;
	/// Per-tier slices into the metadata buffer, indexed by tier id
	tier_locals_t tiers[LLFREE_MAX_TIERS];
} local_t;

size_t ll_local_size(const llfree_tiering_t *tiering)
{
	size_t total = 0;
	for (size_t i = 0; i < tiering->num_tiers; i++)
		total += tiering->tiers[i].count;
	return align_up(sizeof(local_t), LLFREE_CACHE_SIZE) +
	       (sizeof(entry_t) * total);
}

void ll_local_init(local_t *self, const llfree_tiering_t *tiering)
{
	assert(self != NULL);
	assert((size_t)self % LLFREE_CACHE_SIZE == 0);

	self->num_tiers = (uint8_t)tiering->num_tiers;

	// Entries start after the local_t header, cache-line aligned
	entry_t *base =
		(entry_t *)((uint8_t *)self +
			    align_up(sizeof(local_t), LLFREE_CACHE_SIZE));

	// Initialize tier slices
	for (size_t i = 0; i < LLFREE_MAX_TIERS; i++)
		self->tiers[i] = (tier_locals_t){ .entries = NULL, .len = 0 };

	size_t offset = 0;
	for (size_t i = 0; i < tiering->num_tiers; i++) {
		uint8_t tier = tiering->tiers[i].tier;
		size_t count = tiering->tiers[i].count;
		self->tiers[tier] = (tier_locals_t){
			.entries = &base[offset],
			.len = count,
		};
		for (size_t j = 0; j < count; j++) {
			entry_t *entry = &base[offset + j];
			atom_store(&entry->preferred,
				   ll_reserved_new(false, 0, row_id(0)));
#if LLFREE_ENABLE_FREE_RESERVE
			atom_store(&entry->last, ((local_history_t){ 0, 0 }));
#endif
		}
		offset += count;
	}
}

uint8_t ll_local_num_tiers(const local_t *self)
{
	return self->num_tiers;
}

size_t ll_local_mem_size(const local_t *self)
{
	size_t total = 0;
	for (size_t i = 0; i < self->num_tiers; i++)
		total += self->tiers[i].len;
	return align_up(sizeof(local_t), LLFREE_CACHE_SIZE) +
	       (sizeof(entry_t) * total);
}

size_t ll_local_tier_locals(const local_t *self, uint8_t tier)
{
	if (tier >= LLFREE_MAX_TIERS || self->tiers[tier].entries == NULL)
		return LLFREE_LOCAL_NONE;
	return self->tiers[tier].len;
}

static inline local_result_t make_result(bool success, uint8_t tier,
					 reserved_t old)
{
	return (local_result_t){
		.success = success,
		.present = old.present,
		.tier = tier,
		.free = old.free,
		.start_row = row_id(old.start_row),
	};
}

local_result_t ll_local_get(local_t *self, uint8_t tier, size_t index,
			    tree_id_optional_t tree_idx, treeF_t frames)
{
	assert(tier < LLFREE_MAX_TIERS);
	assert(index < self->tiers[tier].len);
	entry_t *entry = &self->tiers[tier].entries[index];
	reserved_t old;
	bool ok = atom_update(&entry->preferred, old, ll_reserved_dec, tree_idx,
			      frames);
	return make_result(ok, tier, old);
}

bool ll_local_put(local_t *self, uint8_t tier, size_t index,
		  tree_id_t tree_idx,
		  treeF_t frames)
{
	assert(tier < LLFREE_MAX_TIERS);
	assert(index < self->tiers[tier].len);
	entry_t *entry = &self->tiers[tier].entries[index];
	reserved_t old;
	return atom_update(&entry->preferred, old, ll_reserved_inc, tree_idx,
			   frames);
}

local_result_t ll_local_set_start(local_t *self, uint8_t tier, size_t index,
				  row_id_t start_row)
{
	assert(tier < LLFREE_MAX_TIERS);
	assert(index < self->tiers[tier].len);
	entry_t *entry = &self->tiers[tier].entries[index];
	reserved_t old;
	bool ok = atom_update(&entry->preferred, old, ll_reserved_set_start,
			      start_row);
	return make_result(ok, tier, old);
}

local_result_t ll_local_swap(local_t *self, uint8_t tier, size_t index,
			     tree_id_t new_tree_idx, treeF_t new_free)
{
	assert(tier < LLFREE_MAX_TIERS);
	assert(index < self->tiers[tier].len);
	entry_t *entry = &self->tiers[tier].entries[index];
	reserved_t new =
		ll_reserved_new(true, new_free, row_from_tree(new_tree_idx));
	reserved_t old;
	atom_update(&entry->preferred, old, ll_reserved_swap, new);
	return make_result(true, tier, old);
}

/// Steal frames from a slot where the policy allows Match or Steal.
/// Iterates tier-by-tier, starting from the requested tier
local_result_t ll_local_steal(local_t *self, uint8_t tier, size_t index,
			      tree_id_optional_t tree_idx, treeF_t frames,
			      llfree_policy_fn policy)
{
	for (size_t i = 0; i < LLFREE_MAX_TIERS; i++) {
		uint8_t target_tier = (uint8_t)((i + tier) % LLFREE_MAX_TIERS);
		tier_locals_t *target = &self->tiers[target_tier];
		if (target->len == 0)
			continue;

		llfree_policy_t p = policy(tier, target_tier, frames);
		if (p.type != LLFREE_POLICY_MATCH &&
		    p.type != LLFREE_POLICY_STEAL)
			continue;

		for (size_t j = 0; j < target->len; j++) {
			size_t jj = (index + j) % target->len;
			reserved_t old;
			bool ok = atom_update(&target->entries[jj].preferred,
					      old, ll_reserved_dec, tree_idx,
					      frames);
			if (ok)
				return make_result(true, target_tier, old);
		}
	}
	return (local_result_t){ .success = false };
}

/// Find a slot where the policy returns Demote, atomically take it, and
/// swap the decremented tree into the requesting local.
demote_any_result_t ll_local_demote_any(local_t *self, uint8_t tier,
					ll_optional_t index,
					tree_id_optional_t tree_idx,
					treeF_t frames,
					llfree_policy_fn policy)
{
	demote_any_result_t fail = { .found = false };

	for (uint8_t i = 1; i < LLFREE_MAX_TIERS; i++) {
		uint8_t target_tier = (uint8_t)((i + tier) % LLFREE_MAX_TIERS);
		tier_locals_t *target = &self->tiers[target_tier];
		if (target->len == 0)
			continue;

		llfree_policy_t p = policy(tier, target_tier, frames);
		if (p.type != LLFREE_POLICY_DEMOTE)
			continue;

		for (size_t j = 0; j < target->len; j++) {
			size_t idx = index.present ? index.value : 0;
			size_t jj = (idx + j) % target->len;

			// Atomically take the slot if present
			reserved_t old;
			bool ok = atom_update(&target->entries[jj].preferred,
					      old, ll_reserved_take);
			if (!ok)
				continue;

			// Check the taken slot has enough free
			if (old.free < frames ||
			    (tree_idx.present &&
			     tree_from_row(row_id(old.start_row)).value !=
				     tree_idx.value.value)) {
				// Put it back
				reserved_t restore = old;
				atom_update(&target->entries[jj].preferred, old,
					    ll_reserved_swap, restore);
				continue;
			}

			// Compute new tree: old minus frames we're about to allocate
			reserved_t new_res = ll_reserved_new(
				true, old.free - frames, row_id(old.start_row));

			// Swap into the requesting local
			demote_any_result_t result = { .found = true,
						       .row = row_id(old.start_row),
						       .old_row = row_id_none() };
			tier_locals_t *req = &self->tiers[tier];
			if (index.present && req->len > 0 && idx < req->len) {
				reserved_t prev;
				atom_update(&req->entries[idx].preferred, prev,
					    ll_reserved_swap, new_res);
				if (prev.present)
					result.old_row = row_id_some(row_id(prev.start_row));
				result.old_tier = tier;
				result.old_free = prev.free;
			}
			return result;
		}
	}
	return fail;
}

#if LLFREE_ENABLE_FREE_RESERVE
static bool frees_inc(local_history_t *self, tree_id_t tree_idx)
{
	if (self->idx != tree_idx.value) {
		// restart for different tree
		self->idx = tree_idx.value;
		self->frees = 1;
		return true;
	}
	if (self->frees < LAST_FREES) {
		// same tree, still counting
		self->frees += 1;
		return true;
	}
	// LAST_FREES threshold reached — signal caller to reserve
	return false;
}
#endif

bool ll_local_free_inc(local_t *self, uint8_t tier, size_t index,
		       tree_id_t tree_idx)
{
#if LLFREE_ENABLE_FREE_RESERVE
	assert(tier < LLFREE_MAX_TIERS);
	assert(index < self->tiers[tier].len);
	entry_t *entry = &self->tiers[tier].entries[index];
	local_history_t frees;
	bool updated = atom_update(&entry->last, frees, frees_inc, tree_idx);
	// !updated means threshold was reached → caller should reserve
	return !updated;
#else
	(void)self;
	(void)tier;
	(void)index;
	(void)tree_idx;
	return false;
#endif
}

local_result_t ll_local_drain(local_t *self, uint8_t tier, size_t index)
{
	assert(tier < LLFREE_MAX_TIERS);
	assert(index < self->tiers[tier].len);
	entry_t *entry = &self->tiers[tier].entries[index];
	reserved_t old;
	atom_update(&entry->preferred, old, ll_reserved_swap,
		    ll_reserved_new(false, 0, row_id(0)));
	return make_result(old.present, tier, old);
}

ll_tree_stats_t ll_local_stats(const local_t *self)
{
	ll_tree_stats_t stats = { 0 };
	for (uint8_t t = 0; t < LLFREE_MAX_TIERS; t++) {
		const tier_locals_t *tl = &self->tiers[t];
		for (size_t j = 0; j < tl->len; j++) {
			reserved_t res = atom_load(&tl->entries[j].preferred);
			if (res.present) {
				stats.free_frames += res.free;
				stats.free_trees += res.free ==
						    LLFREE_TREE_SIZE;
				stats.tiers[t].free_frames += res.free;
			}
		}
	}
	return stats;
}

local_result_t ll_local_stats_at(const local_t *self, tree_id_t tree_idx)
{
	for (uint8_t t = 0; t < LLFREE_MAX_TIERS; t++) {
		const tier_locals_t *tl = &self->tiers[t];
		for (size_t j = 0; j < tl->len; j++) {
			reserved_t res = atom_load(&tl->entries[j].preferred);
			if (!res.present)
				continue;
			if (tree_from_row(row_id(res.start_row)).value ==
			    tree_idx.value)
				return make_result(true, t, res);
		}
	}
	return (local_result_t){
		.success = false,
		.present = false,
		.free = 0,
		.start_row = row_id(0)
	};
}

void ll_local_print(const local_t *self, size_t indent)
{
	if (indent == 0)
		llfree_info_start();
	llfree_info_cont("%sll_local_t {\n", INDENT(indent));
	size_t total = 0;
	for (size_t i = 0; i < self->num_tiers; i++)
		total += self->tiers[i].len;
	llfree_info_cont("%snum_tiers: %u, total: %zu\n", INDENT(indent + 1),
			 self->num_tiers, total);

	for (uint8_t t = 0; t < LLFREE_MAX_TIERS; t++) {
		const tier_locals_t *tl = &self->tiers[t];
		if (tl->len == 0)
			continue;
		llfree_info_cont("%stier %u (%zu entries):\n",
				 INDENT(indent + 1), t, tl->len);
		for (size_t j = 0; j < tl->len; j++) {
			reserved_t res = atom_load(&tl->entries[j].preferred);
			llfree_info_cont("%s[%zu] { present: %d, free: %" PRIu64
					 ", idx: %" PRIuS " }\n",
					 INDENT(indent + 2), j, res.present,
					 (uint64_t)res.free,
					 tree_from_row(row_id(res.start_row)).value);
#if LLFREE_ENABLE_FREE_RESERVE
			local_history_t last = atom_load(&tl->entries[j].last);
			llfree_info_cont("%s  last: { idx: %" PRIu64
					 ", frees: %" PRIuS " }\n",
					 INDENT(indent + 2), last.idx,
					 (size_t)last.frees);
#endif
		}
	}

	llfree_info_cont("%s}\n", INDENT(indent));
	if (indent == 0)
		llfree_info_end();
}

void ll_local_validate(const local_t *self, const llfree_t *llfree,
		       void (*validate_tree)(const llfree_t *llfree,
					     local_result_t res))
{
	assert(self != NULL);
	for (uint8_t t = 0; t < LLFREE_MAX_TIERS; t++) {
		const tier_locals_t *tl = &self->tiers[t];
		for (size_t j = 0; j < tl->len; j++) {
			reserved_t res = atom_load(&tl->entries[j].preferred);
			assert(res.free <= LLFREE_TREE_SIZE);
			if (res.present) {
				validate_tree(llfree,
					      make_result(true, t, res));
			}
		}
	}
}
