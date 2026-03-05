#include "local.h"
#include "llfree.h"
#include "tree.h"
#include "utils.h"

/// CPU-local data of the currently reserved tree
typedef struct reserved {
	/// true if there is a reserved tree
	bool present : 1;
	/// Number of free frames in the tree
	treeF_t free : LLFREE_TREE_ORDER + 1;
	/// Bitfield row index of reserved tree,
	/// used for identifying the reserved tree and as starting point
	/// for the next allocation
	uint64_t start_row : 64 - 2 - LLFREE_TREE_ORDER;
} reserved_t;
_Static_assert(sizeof(reserved_t) == sizeof(uint64_t), "size overflow");

/// Counts last frees in same tree
typedef struct local_history {
	/// Index of the last tree where a frame was freed
	uint64_t idx : 48;
	/// Number of frees in the same tree
	uint16_t frees : 16;
} local_history_t;
_Static_assert(sizeof(local_history_t) == sizeof(uint64_t), "size overflow");

static inline ll_unused local_result_t local_result_reserved(bool success,
							     reserved_t res,
							     llkind_t kind)
{
	return local_result(success, res.present, res.start_row,
			    tree_new(res.present, kind, res.free));
}

static inline ll_unused reserved_t ll_reserved_new(bool present, treeF_t free,
						   uint64_t start_row)
{
	assert(free <= LLFREE_TREE_SIZE);
	return (reserved_t){ .present = present,
			     .free = free,
			     .start_row = start_row };
}

static bool ll_reserved_get(reserved_t *self, ll_optional_t tree_idx,
			    treeF_t free)
{
	if (!self->present)
		return false;
	if (tree_idx.present &&
	    tree_from_row(self->start_row) != tree_idx.value)
		return false;
	if (self->free >= free) {
		self->free -= free;
		return true;
	}
	return false;
}

static bool ll_reserved_put(reserved_t *self, size_t tree_idx, treeF_t free)
{
	if (!self->present || tree_from_row(self->start_row) != tree_idx)
		return false;
	free += self->free;
	assert(free <= LLFREE_TREE_SIZE);
	self->free = free;
	return true;
}

static bool ll_reserved_steal(reserved_t *self, ll_optional_t tree_idx,
			      treeF_t free)
{
	if (ll_reserved_get(self, tree_idx, free)) {
		*self = ll_reserved_new(false, 0, 0);
		return true;
	}
	return false;
}

static bool ll_reserved_set_start(reserved_t *self, uint64_t start_row,
				  bool force)
{
	if (force || (self->present && tree_from_row(self->start_row) ==
					       tree_from_row(start_row))) {
		self->start_row = start_row;
		return true;
	}
	return false;
}

// ----------------------------------------------------------------------------
//
// Local CPU data
//
// ----------------------------------------------------------------------------

/// This represents the local CPU data
typedef struct __attribute__((aligned(LLFREE_CACHE_SIZE))) entry {
	/// Kind of the reservation
	llkind_t kind;
	/// Reserved trees
	_Atomic(reserved_t) reserved;
	/// Counts last frees in same tree
	_Atomic(local_history_t) last;
	// Index of the last reclaimed tree
	_Atomic(uint64_t) last_reclaimed;
} entry_t;

typedef struct __attribute__((aligned(LLFREE_CACHE_SIZE))) local {
	/// Number of entries
	size_t count;
	/// Array of CPU-local entries
	entry_t entries[];
} local_t;

size_t ll_local_size(const llkind_desc_t *kinds)
{
	size_t size = sizeof(local_t);
	for (; kinds->count != 0; kinds++)
		size += kinds->count * sizeof(entry_t);
	return size;
}

void ll_local_init(local_t *self, const llkind_desc_t *kinds)
{
	assert((size_t)self % LLFREE_CACHE_SIZE == 0);

	assert(self != NULL);
	size_t count = 0;
	for (; kinds->count != 0; kinds++) {
		for (size_t i = 0; i < kinds->count; i++) {
			self->entries[count++] = (entry_t){
				.kind = kinds->kind,
				.reserved = ll_reserved_new(false, 0, 0),
				.last = (local_history_t){ 0, 0 },
				.last_reclaimed = 0,
			};
		}
	}
	self->count = count;
}

size_t ll_local_len(const local_t *self)
{
	assert(self != NULL);
	return self->count;
}

llkind_t ll_local_kind(const local_t *self, size_t local)
{
	assert(self != NULL);
	assert(local < self->count);
	return self->entries[local].kind;
}

local_result_t ll_local_get(local_t *self, size_t local, ll_optional_t tree_idx,
			    treeF_t free)
{
	assert(local < self->count);
	reserved_t old;
	entry_t *entry = &self->entries[local];
	bool success = atom_update(&entry->reserved, old, ll_reserved_get,
				   tree_idx, free);
	return local_result_reserved(success, old, entry->kind);
}

bool ll_local_can_get(local_t *self, size_t local,
				ll_optional_t tree_idx, treeF_t free)
{
	assert(local < self->count);
	entry_t *entry = &self->entries[local];
	reserved_t old = atom_load(&entry->reserved);
	return ll_reserved_get(&old, tree_idx, free);
}

bool ll_local_put(local_t *self, size_t local, size_t tree_idx, treeF_t free)
{
	assert(local < self->count);
	entry_t *entry = &self->entries[local];

	reserved_t old;
	bool success = atom_update(&entry->reserved, old, ll_reserved_put,
				   tree_idx, free);

	return success;
}

void ll_local_set_start(local_t *self, size_t local, uint64_t row)
{
	assert(local < self->count);
	entry_t *entry = &self->entries[local];
	reserved_t old;
	atom_update(&entry->reserved, old, ll_reserved_set_start, row, false);
}

local_result_t ll_local_steal(local_t *self, size_t local,
			      ll_optional_t tree_idx, treeF_t free)
{
	assert(local < self->count);

	llkind_t kind = self->entries[local].kind;
	for (size_t c = 0; c < self->count; c++) {
		entry_t *entry = &self->entries[(local + c) % self->count];
		if (entry->kind.id != kind.id)
			continue;
		local_result_t res = ll_local_get(self, c, tree_idx, free);
		if (res.success)
			return res;
	}
	// Steal from lower kind (no demotion necessary)
	for (size_t c = 1; c < self->count; c++) {
		entry_t *entry = &self->entries[(local + c) % self->count];
		if (entry->kind.id >= kind.id)
			continue;
		local_result_t res = ll_local_get(self, c, tree_idx, free);
		if (res.success)
			return res;
	}
	return local_result(false, false, 0, tree_new(false, kind, 0));
}

local_result_t ll_local_steal_downgrade(local_t *self, size_t local,
					ll_optional_t tree_idx, treeF_t free)
{
	assert(local < self->count);
	llkind_t kind = self->entries[local].kind;
	for (size_t c = 1; c < self->count; c++) {
		entry_t *entry = &self->entries[(local + c) % self->count];
		if (entry->kind.id <= kind.id)
			continue;
		reserved_t res;
		bool success = atom_update(&entry->reserved, res,
					   ll_reserved_steal, tree_idx, free);
		if (!success)
			continue;

		// Update entry
		success = ll_reserved_get(&res, tree_idx, free);
		assert(success);

		// Replace local tree and return its free count for unreservation
		reserved_t old = atom_swap(&self->entries[local].reserved, res);
		return local_result_reserved(true, old, entry->kind);
	}
	return local_result(false, false, 0, tree_new(false, kind, 0));
}

local_result_t ll_local_swap(local_t *self, size_t local, size_t tree_idx,
			     treeF_t free)
{
	assert(local < self->count);
	entry_t *entry = &self->entries[local];
	reserved_t new = ll_reserved_new(true, free, row_from_tree(tree_idx));
	reserved_t old = atom_swap(&entry->reserved, new);
	return local_result_reserved(true, old, entry->kind);
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
		// same tree
		self->frees += 1;
		return true;
	}
	return false;
}

bool ll_local_free_inc(local_t *self, size_t local, size_t tree_idx)
{
	assert(local < self->count);
	entry_t *entry = &self->entries[local];
	local_history_t frees;
	bool updated = atom_update(&entry->last, frees, frees_inc, tree_idx);
	return !updated;
}

/// Unreserve a local tree
local_result_t ll_local_drain(local_t *self, size_t local)
{
	assert(local < self->count);
	entry_t *entry = &self->entries[local];
	reserved_t old =
		atom_swap(&entry->reserved, ll_reserved_new(false, 0, 0));
	return local_result_reserved(true, old, entry->kind);
}

ll_stats_t ll_local_stats(const local_t *self)
{
	ll_stats_t stats = { 0, 0, 0, 0, 0, 0 };
	for (size_t c = 0; c < self->count; c++) {
		const entry_t *entry = &self->entries[c];
		reserved_t res = atom_load(&entry->reserved);
		if (!res.present)
			continue;
		stats.free_frames += res.free;
		if (entry->kind.id >= LLKIND_HUGE.id)
			stats.free_huge += res.free >> LLFREE_HUGE_ORDER;
		if (entry->kind.id >= LLKIND_ZERO.id)
			stats.zeroed_huge += res.free >> LLFREE_HUGE_ORDER;
	}
	return stats;
}

ll_stats_t ll_local_stats_at(const local_t *self, size_t tree_idx)
{
	ll_stats_t stats = { 0, 0, 0, 0, 0, 0 };
	for (size_t c = 0; c < self->count; c++) {
		const entry_t *entry = &self->entries[c];
		reserved_t res = atom_load(&entry->reserved);
		if (!res.present)
			continue;
		if (tree_from_row(res.start_row) != tree_idx)
			continue;

		stats.free_frames = res.free;
		if (entry->kind.id >= LLKIND_HUGE.id)
			stats.free_huge = res.free >> LLFREE_HUGE_ORDER;
		if (entry->kind.id >= LLKIND_ZERO.id)
			stats.zeroed_huge = res.free >> LLFREE_HUGE_ORDER;
		return stats;
	}
	return stats;
}

size_t ll_local_reclaimed(const local_t *self, size_t local)
{
	assert(local < self->count);
	return atom_load(&self->entries[local].last_reclaimed);
}
void ll_local_set_reclaimed(local_t *self, size_t local, size_t reclaimed_idx)
{
	assert(local < self->count);
	atom_store(&self->entries[local].last_reclaimed, reclaimed_idx);
}

void ll_local_print(const local_t *self, size_t indent)
{
	if (indent == 0)
		llfree_info_start();
	llfree_info_cont("%sll_local_t {\n", INDENT(indent));
	llfree_info_cont("%scount: %zu\n", INDENT(indent + 1), self->count);

	for (size_t i = 0; i < self->count; ++i) {
		llfree_info_cont("%sentry[%zu] {\n", INDENT(indent + 1), i);
		const entry_t *entry = &self->entries[i];
		reserved_t res = atom_load(&entry->reserved);
		llfree_info_cont("%skind(%" PRIuS
				 "):\t{ present: %d, free: %" PRIu64
				 ", idx: %" PRIuS " }\n",
				 INDENT(indent + 2), (size_t)entry->kind.id,
				 res.present, (uint64_t)res.free,
				 tree_from_row(res.start_row));
		local_history_t last = atom_load(&entry->last);
		llfree_info_cont("%slast: { idx: %" PRIu64 ", frees: %" PRIuS
				 " }\n",
				 INDENT(indent + 2), last.idx,
				 (size_t)last.frees);
		llfree_info_cont(
			"%slast_reclaimed: %" PRIuS "\n", INDENT(indent + 2),
			tree_from_frame(atom_load(&entry->last_reclaimed)));
		llfree_info_cont("%s}\n", INDENT(indent + 1));
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
	for (size_t i = 0; i < self->count; i++) {
		const entry_t *entry = &self->entries[i];
		reserved_t res = atom_load(&entry->reserved);
		assert(res.free <= LLFREE_TREE_SIZE);
		if (res.present) {
			tree_t tree = tree_new(true, entry->kind, res.free);
			validate_tree(llfree,
				      local_result(true, true, res.start_row,
						   tree));
		}
	}
}
