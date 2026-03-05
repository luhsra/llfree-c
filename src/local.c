#include "local.h"
#include "llfree.h"
#include "tree.h"
#include "utils.h"

/// CPU-local data of the currently reserved tree
typedef struct reserved {
	/// true if there is a reserved tree
	bool present : 1;
	/// Number of free frames in the tree (matches tree_t.free width)
	treeF_t free : LLFREE_TREE_FREE_BITS;
	/// Bitfield row index of reserved tree,
	/// used for identifying the reserved tree and as starting point
	/// for the next allocation
	uint64_t start_row : 64 - LLFREE_TREE_FREE_BITS - 1;
} reserved_t;
_Static_assert(sizeof(reserved_t) == sizeof(uint64_t), "size overflow");

/// Counts last frees in same tree
typedef struct local_history {
	/// Index of the last tree where a frame was freed
	uint64_t idx : 48;
	/// Number of consecutive frees in the same tree
	uint16_t frees : 16;
} local_history_t;
_Static_assert(sizeof(local_history_t) == sizeof(uint64_t), "size overflow");

static inline reserved_t ll_reserved_new(bool present, treeF_t free,
					 uint64_t start_row)
{
	assert(free <= LLFREE_TREE_SIZE);
	return (reserved_t){ present, free, start_row };
}

static bool ll_reserved_dec(reserved_t *self, ll_optional_t tree_idx,
			    treeF_t frames)
{
	if (!self->present)
		return false;
	if (tree_idx.present &&
	    tree_from_row(self->start_row) != tree_idx.value)
		return false;
	if (self->free < frames)
		return false;
	self->free -= frames;
	return true;
}

static bool ll_reserved_inc(reserved_t *self, size_t tree_idx, treeF_t frames)
{
	if (!self->present || tree_from_row(self->start_row) != tree_idx)
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

static bool ll_reserved_set_start(reserved_t *self, uint64_t start_row)
{
	if (!self->present ||
	    tree_from_row(self->start_row) != tree_from_row(start_row))
		return false;
	self->start_row = start_row;
	return true;
}

/// Atomically take a present reservation (clears it)
static bool ll_reserved_take(reserved_t *self)
{
	if (!self->present)
		return false;
	*self = ll_reserved_new(false, 0, 0);
	return true;
}

// ----------------------------------------------------------------------------
//
// Local CPU data
//
// ----------------------------------------------------------------------------

/// One local slot per (tier × local_per_tier).
/// Flat layout: [tier0_slot0..tier0_slot(count0-1), tier1_slot0..]
typedef struct __attribute__((aligned(LLFREE_CACHE_SIZE))) entry {
	/// Tier this slot manages
	uint8_t tier;
	char _pad[7];
	/// Currently reserved tree for this slot
	_Atomic(reserved_t) preferred;
	/// Counts recent frees to the same tree (heuristic for reserving)
	_Atomic(local_history_t) last;
} entry_t;
_Static_assert(sizeof(entry_t) <= LLFREE_CACHE_SIZE,
	       "entry_t exceeds cache line");

typedef struct __attribute__((aligned(LLFREE_CACHE_SIZE))) local {
	/// Total number of local slots = sum(tiering->tiers[i].count)
	size_t len;
	/// Number of tiers (used for huge-tier detection in stats)
	uint8_t num_tiers;
	/// Array of `len` entries
	entry_t entries[];
} local_t;

size_t ll_local_size(const llfree_tiering_t *tiering)
{
	size_t total = 0;
	for (size_t i = 0; i < tiering->num_tiers; i++)
		total += tiering->tiers[i].count;
	return align_up(sizeof(local_t) + (sizeof(entry_t) * total),
			LLFREE_CACHE_SIZE);
}

void ll_local_init(local_t *self, const llfree_tiering_t *tiering)
{
	assert(self != NULL);
	assert((size_t)self % LLFREE_CACHE_SIZE == 0);

	size_t total = 0;
	for (size_t i = 0; i < tiering->num_tiers; i++)
		total += tiering->tiers[i].count;
	self->len = total;
	self->num_tiers = (uint8_t)tiering->num_tiers;

	size_t slot = 0;
	for (size_t i = 0; i < tiering->num_tiers; i++) {
		for (size_t j = 0; j < tiering->tiers[i].count; j++, slot++) {
			entry_t *entry = &self->entries[slot];
			entry->tier = tiering->tiers[i].tier;
			atom_store(&entry->preferred,
				   ll_reserved_new(false, 0, 0));
			atom_store(&entry->last, ((local_history_t){ 0, 0 }));
		}
	}
}

uint8_t ll_local_num_tiers(const local_t *self)
{
	return self->num_tiers;
}

size_t ll_local_len(const local_t *self)
{
	return self->len;
}

size_t ll_local_mem_size(const local_t *self)
{
	return align_up(sizeof(local_t) + (sizeof(entry_t) * self->len),
			LLFREE_CACHE_SIZE);
}

uint8_t ll_local_tier(const local_t *self, size_t slot)
{
	return self->entries[slot].tier;
}

static inline local_result_t make_result(bool success, const entry_t *entry,
					 reserved_t old)
{
	return (local_result_t){
		.success = success,
		.present = old.present,
		.tier = entry->tier,
		.free = old.free,
		.start_row = old.start_row,
	};
}

local_result_t ll_local_get(local_t *self, size_t slot,
			    ll_optional_t tree_idx, treeF_t frames)
{
	assert(slot < ll_local_len(self));
	entry_t *entry = &self->entries[slot];
	reserved_t old;
	bool ok = atom_update(&entry->preferred, old, ll_reserved_dec, tree_idx,
			      frames);
	return make_result(ok, entry, old);
}

bool ll_local_put(local_t *self, size_t slot, size_t tree_idx, treeF_t frames)
{
	assert(slot < ll_local_len(self));
	entry_t *entry = &self->entries[slot];
	reserved_t old;
	return atom_update(&entry->preferred, old, ll_reserved_inc, tree_idx,
			   frames);
}

local_result_t ll_local_set_start(local_t *self, size_t slot,
				  uint64_t start_row)
{
	assert(slot < ll_local_len(self));
	entry_t *entry = &self->entries[slot];
	reserved_t old;
	bool ok = atom_update(&entry->preferred, old, ll_reserved_set_start,
			      start_row);
	return make_result(ok, entry, old);
}

local_result_t ll_local_swap(local_t *self, size_t slot, size_t new_tree_idx,
			     treeF_t new_free)
{
	assert(slot < ll_local_len(self));
	entry_t *entry = &self->entries[slot];
	reserved_t new =
		ll_reserved_new(true, new_free, row_from_tree(new_tree_idx));
	reserved_t old;
	atom_update(&entry->preferred, old, ll_reserved_swap, new);
	return make_result(true, entry, old);
}

/// Steal frames from a slot where the policy allows Match or Steal.
local_result_t ll_local_steal(local_t *self, size_t slot,
			      ll_optional_t tree_idx, treeF_t frames,
			      llfree_policy_fn policy)
{
	assert(slot < ll_local_len(self));
	uint8_t requested = self->entries[slot].tier;
	size_t total = ll_local_len(self);

	for (size_t i = 1; i < total; i++) {
		size_t idx = (slot + i) % total;
		entry_t *candidate = &self->entries[idx];
		reserved_t res = atom_load(&candidate->preferred);
		if (!res.present)
			continue;
		llfree_policy_t p =
			policy(requested, candidate->tier, res.free);
		if (p.type != LLFREE_POLICY_MATCH &&
		    p.type != LLFREE_POLICY_STEAL)
			continue;
		reserved_t old;
		bool ok = atom_update(&candidate->preferred, old,
				      ll_reserved_dec, tree_idx, frames);
		if (ok)
			return make_result(true, candidate, old);
	}
	return (local_result_t){ .success = false };
}

/// Find a slot where the policy returns Demote, atomically take it, and
/// return its info so the caller can update the global tree tier.
/// If old_out != NULL, it receives the taken slot's old info.
local_result_t ll_local_steal_demote(local_t *self, size_t slot,
				     llfree_policy_fn policy,
				     local_result_t *old_out)
{
	assert(slot < ll_local_len(self));
	uint8_t requested = self->entries[slot].tier;
	size_t total = ll_local_len(self);

	for (size_t i = 1; i < total; i++) {
		size_t idx = (slot + i) % total;
		entry_t *candidate = &self->entries[idx];
		reserved_t res = atom_load(&candidate->preferred);
		if (!res.present)
			continue;
		llfree_policy_t p =
			policy(requested, candidate->tier, res.free);
		if (p.type != LLFREE_POLICY_DEMOTE)
			continue;
		// Atomically clear the slot
		reserved_t old;
		bool ok = atom_update(&candidate->preferred, old,
				      ll_reserved_take);
		if (ok) {
			local_result_t taken =
				make_result(true, candidate, old);
			if (old_out)
				*old_out = taken;
			// Return as if we now own the tree for the requested tier
			return (local_result_t){
				.success = true,
				.present = old.present,
				.tier = requested,
				.free = old.free,
				.start_row = old.start_row,
			};
		}
	}
	return (local_result_t){ .success = false };
}

static bool frees_inc(local_history_t *self, size_t tree_idx)
{
	if (self->idx != tree_idx) {
		// restart for different tree
		self->idx = tree_idx;
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

bool ll_local_free_inc(local_t *self, size_t slot, size_t tree_idx)
{
	assert(slot < ll_local_len(self));
	entry_t *entry = &self->entries[slot];
	local_history_t frees;
	bool updated = atom_update(&entry->last, frees, frees_inc, tree_idx);
	// !updated means threshold was reached → caller should reserve
	return !updated;
}

void ll_local_drain(local_t *self, size_t slot)
{
	assert(slot < ll_local_len(self));
	entry_t *entry = &self->entries[slot];
	reserved_t old;
	atom_update(&entry->preferred, old, ll_reserved_swap,
		    ll_reserved_new(false, 0, 0));
}

ll_tree_stats_t ll_local_stats(const local_t *self, ll_tier_stats_t *tiers,
			       size_t tier_len)
{
	ll_tree_stats_t stats = { 0, 0 };
	for (size_t i = 0; i < self->len; i++) {
		const entry_t *entry = &self->entries[i];
		reserved_t res = atom_load(&entry->preferred);
		if (res.present) {
			stats.free_frames += res.free;
			stats.free_trees += res.free == LLFREE_TREE_SIZE;
			if (entry->tier < tier_len) {
				tiers[entry->tier].free_frames += res.free;
				tiers[entry->tier].alloc_frames +=
					LLFREE_TREE_SIZE - res.free;
			}
		}
	}
	return stats;
}

local_result_t ll_local_stats_at(const local_t *self, size_t tree_idx)
{
	size_t total = ll_local_len(self);
	for (size_t i = 0; i < total; i++) {
		const entry_t *entry = &self->entries[i];
		reserved_t res = atom_load(&entry->preferred);
		if (!res.present)
			continue;
		if (tree_from_row(res.start_row) == tree_idx) {
			return make_result(true, entry, res);
		}
	}
	return (local_result_t){
		.success = false, .present = false, .free = 0, .start_row = 0
	};
}

void ll_local_print(const local_t *self, size_t indent)
{
	if (indent == 0)
		llfree_info_start();
	llfree_info_cont("%sll_local_t {\n", INDENT(indent));
	llfree_info_cont("%slen: %zu, num_tiers: %u\n", INDENT(indent + 1),
			 self->len, self->num_tiers);

	size_t total = ll_local_len(self);
	for (size_t i = 0; i < total; i++) {
		const entry_t *entry = &self->entries[i];
		reserved_t res = atom_load(&entry->preferred);
		llfree_info_cont(
			"%sentry[%zu] tier=%u { present: %d, free: %" PRIu64
			", idx: %" PRIuS " }\n",
			INDENT(indent + 1), i, entry->tier, res.present,
			(uint64_t)res.free, tree_from_row(res.start_row));
		local_history_t last = atom_load(&entry->last);
		llfree_info_cont("%s  last: { idx: %" PRIu64 ", frees: %" PRIuS
				 " }\n",
				 INDENT(indent + 1), last.idx,
				 (size_t)last.frees);
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
	size_t total = ll_local_len(self);
	for (size_t i = 0; i < total; i++) {
		const entry_t *entry = &self->entries[i];
		reserved_t res = atom_load(&entry->preferred);
		assert(res.free <= LLFREE_TREE_SIZE);
		if (res.present) {
			validate_tree(llfree, make_result(true, entry, res));
		}
	}
}
