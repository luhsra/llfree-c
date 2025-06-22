#include "local.h"
#include "tree.h"
#include "utils.h"

/// CPU-local data of the currently reserved tree
typedef struct reserved {
	/// true if there is a reserved tree
	bool present : 1;
	/// Number of free frames in the tree
	treeF_t free : LLFREE_TREE_ORDER + 1;
	/// Number of zeroed huge frames in the tree
	treeF_t zeroed : LLFREE_TREE_CHILDREN_ORDER + 1;
	/// Bitfield row index of reserved tree,
	/// used for identifying the reserved tree and as starting point
	/// for the next allocation
	uint64_t start_row : 64 - (LLFREE_TREE_ORDER + 1) -
		(LLFREE_TREE_CHILDREN_ORDER + 1) - 1;
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

/// Represents a reserved tree kind
typedef struct r_kind {
	uint8_t id;
} r_kind_t;
/// Number of tree kinds
#define RESERVED_KINDS (size_t)(4u)
static inline ll_unused r_kind_t r_kind(uint8_t id)
{
	assert(id < RESERVED_KINDS);
	return (r_kind_t){ id };
}
/// Contains immovable pages
#define RESERVED_FIXED r_kind(0)
/// Contains movable pages
#define RESERVED_MOVABLE r_kind(1)
/// Contains huge pages (movability is irrelevant)
#define RESERVED_HUGE r_kind(2)
/// Contains at least one zeroed huge page
#define RESERVED_ZEROED r_kind(3)

static inline ll_unused char *r_kind_name(r_kind_t kind)
{
	assert(kind.id < RESERVED_KINDS);
	return ((char *[]){ "fixed", "movable", "huge", "zeroed" })[kind.id];
}

static inline ll_unused r_kind_t r_kind_flags(llflags_t flags)
{
	if (flags.order >= LLFREE_HUGE_ORDER)
		return flags.zeroed ? RESERVED_ZEROED : RESERVED_HUGE;
	if (flags.movable)
		return RESERVED_MOVABLE;
	return RESERVED_FIXED;
}
static inline ll_unused r_kind_t r_kind_change(tree_change_t change)
{
	if (change.kind.id == RESERVED_HUGE.id) {
		return change.zeroed ? RESERVED_ZEROED : RESERVED_HUGE;
	}
	if (change.kind.id == RESERVED_MOVABLE.id) {
		return RESERVED_MOVABLE;
	}
	return RESERVED_FIXED;
}
static inline ll_unused r_kind_t r_kind_tree(tree_t tree)
{
	if (tree.kind == TREE_FIXED.id)
		return RESERVED_FIXED;
	if (tree.kind == TREE_MOVABLE.id)
		return RESERVED_MOVABLE;
	return tree.zeroed ? RESERVED_ZEROED : RESERVED_HUGE;
}

static inline ll_unused tree_kind_t r_kind_to_tree(r_kind_t kind)
{
	if (kind.id == RESERVED_FIXED.id)
		return TREE_FIXED;
	if (kind.id == RESERVED_MOVABLE.id)
		return TREE_MOVABLE;
	return TREE_HUGE;
}

static inline ll_unused local_result_t local_result_reserved(bool success,
							     reserved_t res,
							     r_kind_t kind)
{
	return local_result(success, res.present, res.start_row,
			    tree_new(res.present, r_kind_to_tree(kind),
				     res.free, res.zeroed));
}

static inline ll_unused reserved_t ll_reserved_new(bool present, treeF_t free,
						   treeF_t zeroed,
						   uint64_t start_row)
{
	assert(free <= LLFREE_TREE_SIZE);
	assert(zeroed <= LLFREE_TREE_CHILDREN);
	return (reserved_t){ present, free, zeroed, start_row };
}

static bool ll_reserved_get(reserved_t *self, optional_size_t tree_idx,
			    tree_change_t change)
{
	if (!self->present)
		return false;
	if (tree_idx.present &&
	    tree_from_row(self->start_row) != tree_idx.value)
		return false;

	if (change.kind.id == TREE_HUGE.id) {
		treeF_t diff = (change.huge << LLFREE_HUGE_ORDER);
		if (self->free >= diff && self->zeroed >= change.zeroed) {
			self->free -= diff;
			self->zeroed -= change.zeroed;

			treeF_t huge = self->free >> LLFREE_CHILD_ORDER;
			if (huge < self->zeroed)
				self->zeroed = huge;

			return true;
		}
	} else {
		if (self->free >= change.frames) {
			self->free -= change.frames;
			return true;
		}
	}
	return false;
}

static bool ll_reserved_put(reserved_t *self, size_t tree_idx,
			    tree_change_t change)
{
	if (!self->present || tree_from_row(self->start_row) != tree_idx)
		return false;

	if (change.kind.id == TREE_HUGE.id) {
		treeF_t free = self->free + (change.huge << LLFREE_HUGE_ORDER);
		treeF_t zeroed = self->zeroed + change.zeroed;
		assert(free <= LLFREE_TREE_SIZE);
		assert(zeroed <= LLFREE_TREE_CHILDREN);
		self->free = free;
		self->zeroed = zeroed;
	} else {
		treeF_t free = self->free + change.frames;
		assert(free <= LLFREE_TREE_SIZE);
		self->free = free;
	}
	return true;
}

static bool ll_reserved_take(reserved_t *self, size_t tree_idx)
{
	if (tree_from_row(self->start_row) == tree_idx) {
		*self = ll_reserved_new(false, 0, 0, 0);
		return true;
	}
	return false;
}

static bool ll_reserved_swap(reserved_t *self, reserved_t new)
{
	*self = new;
	return true;
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
	/// Counts last frees in same tree
	_Atomic(local_history_t) last;
	/// Reserved trees
	_Atomic(reserved_t) reserved[RESERVED_KINDS];
	// Index of the last reclaimed tree
	_Atomic(uint64_t) last_reclaimed;
} entry_t;

typedef struct __attribute__((aligned(LLFREE_CACHE_SIZE))) local {
	/// Number of cores
	size_t cores;
	/// Array of CPU-local entries
	entry_t entries[];
} local_t;

size_t ll_local_size(size_t cores)
{
	return align_up(sizeof(local_t) + (sizeof(entry_t) * cores),
			LLFREE_CACHE_SIZE);
}

void ll_local_init(local_t *self, size_t cores)
{
	assert((size_t)self % LLFREE_CACHE_SIZE == 0);
	self->cores = cores;
	assert(self != NULL);
	for (size_t core = 0; core < cores; core++) {
		entry_t *entry = &self->entries[core];
		entry->last_reclaimed = 0;
		entry->last = (local_history_t){ 0, 0 };
		for (size_t i = 0; i < RESERVED_KINDS; ++i) {
			entry->reserved[i] = ll_reserved_new(false, 0, 0, 0);
		}
	}
}

size_t ll_local_cores(local_t *self)
{
	assert(self != NULL);
	return self->cores;
}

static local_result_t ll_local_get_raw(entry_t *self, r_kind_t kind,
				       tree_change_t change,
				       optional_size_t tree_idx)
{
	reserved_t old;
	bool success = atom_update(&self->reserved[kind.id], old,
				   ll_reserved_get, tree_idx, change);
	return local_result_reserved(success, old, kind);
}

local_result_t ll_local_get(local_t *self, size_t core, tree_change_t change,
			    optional_size_t tree_idx)
{
	entry_t *entry = &self->entries[core % self->cores];
	// Try decrementing the local counter
	return ll_local_get_raw(entry, r_kind_change(change), change, tree_idx);
}

bool ll_local_put(local_t *self, size_t core, tree_change_t change,
		  size_t tree_idx)
{
	entry_t *entry = &self->entries[core % self->cores];
	reserved_t old;
	r_kind_t kind = r_kind_change(change);
	bool success = atom_update(&entry->reserved[kind.id], old,
				   ll_reserved_put, tree_idx, change);
	if (!success && kind.id >= RESERVED_HUGE.id) {
		// try other zeroed/non-zeroed reservation
		r_kind_t other_kind = (kind.id == RESERVED_ZEROED.id) ?
					      RESERVED_HUGE :
					      RESERVED_ZEROED;
		return atom_update(&entry->reserved[other_kind.id], old,
				   ll_reserved_put, tree_idx, change);
	}
	return success;
}

local_result_t ll_local_set_start(local_t *self, size_t core,
				  tree_change_t previous_change,
				  uint64_t start_row)
{
	entry_t *entry = &self->entries[core % self->cores];
	reserved_t old;
	r_kind_t kind = r_kind_change(previous_change);
	bool success = atom_update(&entry->reserved[kind.id], old,
				   ll_reserved_set_start, start_row, false);
	return local_result_reserved(success, old, kind);
}

local_result_t ll_local_steal(local_t *self, size_t core, tree_change_t change,
			      bool demote, optional_size_t tree_idx)
{
	r_kind_t kind = r_kind_change(change);

	size_t start = demote ? kind.id + 1 : 0;
	size_t end = demote ? RESERVED_KINDS : kind.id;
	for (size_t k = start; k < end; k++) {
		r_kind_t t_kind = r_kind(k);
		// Just try allocating from the stricter reserved tree
		for (size_t c = core; c < self->cores; c++) {
			entry_t *entry = &self->entries[c % self->cores];
			local_result_t res = ll_local_get_raw(entry, t_kind,
							      change, tree_idx);
			if (res.success)
				return res;
		}
	}
	return local_result(false, false, 0,
			    tree_new(false, r_kind_to_tree(kind), 0, 0));
}

local_result_t ll_local_demote(local_t *self, size_t core,
			       tree_change_t previous_change, size_t tree_idx)
{
	r_kind_t kind = r_kind_change(previous_change);
	assert(kind.id < RESERVED_ZEROED.id);

	for (size_t c = core; c < self->cores; c++) {
		entry_t *entry = &self->entries[c % self->cores];
		for (size_t k = 1; k < RESERVED_KINDS; k++) {
			r_kind_t t_kind =
				r_kind((kind.id + k) % RESERVED_KINDS);
			reserved_t old;
			if (atom_update(&entry->reserved[t_kind.id], old,
					ll_reserved_take, tree_idx)) {
				return local_result_reserved(true, old, kind);
			}
		}
	}
	return local_result(false, false, 0,
			    tree_new(false, r_kind_to_tree(kind), 0, 0));
}

local_result_t ll_local_swap(local_t *self, size_t core,
			     tree_change_t previous_change, size_t new_idx,
			     tree_t new_tree)
{
	r_kind_t kind = r_kind_change(previous_change);
	// if the new tree has a lower kind, use it
	r_kind_t other_kind = r_kind_tree(new_tree);
	kind = r_kind(LL_MIN(kind.id, other_kind.id));

	reserved_t old;
	reserved_t new = ll_reserved_new(
		true, new_tree.free,
		kind.id == RESERVED_ZEROED.id ? new_tree.zeroed : 0,
		row_from_tree(new_idx));
	entry_t *entry = &self->entries[core % self->cores];
	atom_update(&entry->reserved[kind.id], old, ll_reserved_swap, new);
	return local_result_reserved(true, old, kind);
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

bool ll_local_free_inc(local_t *self, size_t core, size_t tree_idx)
{
	entry_t *entry = &self->entries[core % self->cores];
	local_history_t frees;
	bool updated = atom_update(&entry->last, frees, frees_inc, tree_idx);
	return !updated;
}

/// Unreserve all local trees for a core
void ll_local_drain(local_t *self, size_t core)
{
	entry_t *entry = &self->entries[core % self->cores];
	for (size_t k = 0; k < RESERVED_KINDS; k++) {
		reserved_t old;
		atom_update(&entry->reserved[k], old, ll_reserved_swap,
			    ll_reserved_new(false, 0, 0, 0));
	}
}

size_t ll_local_free_frames(local_t *self)
{
	size_t total = 0;
	for (size_t i = 0; i < self->cores; i++) {
		entry_t *entry = &self->entries[i];
		for (size_t j = 0; j < RESERVED_KINDS; j++) {
			reserved_t res = atom_load(&entry->reserved[j]);
			if (res.present)
				total += res.free;
		}
	}
	return total;
}

size_t ll_local_reclaimed(local_t *self, size_t core)
{
	return atom_load(&self->entries[core % self->cores].last_reclaimed);
}
void ll_local_set_reclaimed(local_t *self, size_t core, size_t reclaimed_idx)
{
	atom_store(&self->entries[core % self->cores].last_reclaimed,
		   reclaimed_idx);
}

void ll_local_print(local_t *self, size_t indent)
{
	if (indent == 0)
		llfree_info_start();
	llfree_info_cont("%sll_local_t {\n", INDENT(indent));
	llfree_info_cont("%scores: %zu\n", INDENT(indent + 1), self->cores);

	for (size_t i = 0; i < self->cores; ++i) {
		llfree_info_cont("%sentry[%zu] {\n", INDENT(indent + 1), i);
		entry_t *entry = &self->entries[i];
		for (size_t j = 0; j < RESERVED_KINDS; ++j) {
			reserved_t res = atom_load(&entry->reserved[j]);
			llfree_info_cont("%s%s:\t{ present: %d, free: %" PRIu64
					 ", zeroed: %" PRIuS ", idx: %" PRIuS
					 " }\n",
					 INDENT(indent + 2),
					 r_kind_name(r_kind(j)), res.present,
					 (uint64_t)res.free, (size_t)res.zeroed,
					 tree_from_row(res.start_row));
		}
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
void ll_local_validate(local_t *self, llfree_t *llfree,
		       void (*validate_tree)(llfree_t *llfree,
					     local_result_t res))
{
	assert(self != NULL);
	for (size_t i = 0; i < self->cores; i++) {
		entry_t *entry = &self->entries[i];
		for (size_t j = 0; j < RESERVED_KINDS; j++) {
			reserved_t res = atom_load(&entry->reserved[j]);
			assert(res.free <= LLFREE_TREE_SIZE);
			assert(res.zeroed <= LLFREE_TREE_CHILDREN);
			if (res.present) {
				tree_t tree = tree_new(
					true, r_kind_to_tree(r_kind(j)),
					res.free, res.zeroed);
				validate_tree(llfree,
					      local_result(true, true,
							   res.start_row,
							   tree));
			}
		}
	}
}
