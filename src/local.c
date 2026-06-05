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
	if (tree_idx.present && tree_from_row(row_id(self->start_row)).value !=
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
	if (!self->present || tree_from_row(row_id(self->start_row)).value !=
				      tree_from_row(start_row).value)
		return false;
	self->start_row = start_row.value;
	return true;
}

/// Atomically take a present reservation (clears it)
static bool ll_reserved_take(reserved_t *self, tree_id_optional_t tree_idx,
			     treeF_t frames)
{
	if (ll_reserved_dec(self, tree_idx, frames)) {
		*self = ll_reserved_new(false, 0, row_id(0));
		return true;
	}
	return false;
}

// ----------------------------------------------------------------------------
//
// Local CPU data
//
// ----------------------------------------------------------------------------

/// One local entry per core per cluster
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

/// Slice of entries for one cluster (stored as offset into metadata buffer)
typedef struct cluster_locals {
	size_t offset; // byte offset from local base to first entry
	ll_optional_t len; // ll_none() if cluster not configured
} cluster_locals_t;

/// Locals struct
typedef struct __attribute__((aligned(LLFREE_CACHE_SIZE))) local {
	/// Number of clusters
	uint8_t num_clusters;
	/// Per-cluster slices into the metadata buffer, indexed by cluster id
	cluster_locals_t clusters[LLFREE_MAX_CLUSTERS];
} local_t;

size_t ll_local_size(const llfree_clustering_t *clustering)
{
	size_t total = 0;
	for (size_t i = 0; i < clustering->num_clusters; i++)
		total += clustering->clusters[i].count;
	return align_up(sizeof(local_t), LLFREE_CACHE_SIZE) +
	       (sizeof(entry_t) * total);
}

void ll_local_init(local_t *self, const llfree_clustering_t *clustering)
{
	assert(self != NULL);
	assert((size_t)self % LLFREE_CACHE_SIZE == 0);

	self->num_clusters = (uint8_t)clustering->num_clusters;

	// Entries start after the local_t header, cache-line aligned
	size_t base_offset = align_up(sizeof(local_t), LLFREE_CACHE_SIZE);

	// Initialize cluster slices
	for (size_t i = 0; i < LLFREE_MAX_CLUSTERS; i++)
		self->clusters[i] =
			(cluster_locals_t){ .offset = 0, .len = ll_none() };

	size_t offset = 0;
	for (size_t i = 0; i < clustering->num_clusters; i++) {
		uint8_t cluster = clustering->clusters[i].cluster;
		size_t count = clustering->clusters[i].count;
		self->clusters[cluster] = (cluster_locals_t){
			.offset = base_offset + (sizeof(entry_t) * offset),
			.len = ll_some(count),
		};
		for (size_t j = 0; j < count; j++) {
			entry_t *entry = (entry_t *)((uint8_t *)self +
						     self->clusters[cluster].offset) +
					 j;
			atom_store(&entry->preferred,
				   ll_reserved_new(false, 0, row_id(0)));
#if LLFREE_ENABLE_FREE_RESERVE
			atom_store(&entry->last, ((local_history_t){ 0, 0 }));
#endif
		}
		offset += count;
	}
}

uint8_t ll_local_num_clusters(const local_t *self)
{
	return self->num_clusters;
}

size_t ll_local_mem_size(const local_t *self)
{
	size_t total = 0;
	for (size_t i = 0; i < self->num_clusters; i++)
		total += self->clusters[i].len.value;
	return align_up(sizeof(local_t), LLFREE_CACHE_SIZE) +
	       (sizeof(entry_t) * total);
}

ll_optional_t ll_local_cluster_locals(const local_t *self, uint8_t cluster)
{
	if (cluster >= LLFREE_MAX_CLUSTERS)
		return ll_none();
	return self->clusters[cluster].len;
}

static inline local_result_t make_result(bool success, uint8_t cluster,
					 reserved_t old)
{
	return (local_result_t){
		.success = success,
		.present = old.present,
		.cluster = cluster,
		.free = old.free,
		.start_row = row_id(old.start_row),
	};
}

local_result_t ll_local_get(local_t *self, uint8_t cluster, size_t index,
			    tree_id_optional_t tree_idx, treeF_t frames)
{
	assert(cluster < LLFREE_MAX_CLUSTERS);
	assert(self->clusters[cluster].len.present &&
	       index < self->clusters[cluster].len.value);
	entry_t *entry =
		((entry_t *)((uint8_t *)self + self->clusters[cluster].offset)) + index;
	reserved_t old;
	bool ok = atom_update(&entry->preferred, old, ll_reserved_dec, tree_idx,
			      frames);
	return make_result(ok, cluster, old);
}

bool ll_local_put(local_t *self, uint8_t cluster, size_t index, tree_id_t tree_idx,
		  treeF_t frames)
{
	assert(cluster < LLFREE_MAX_CLUSTERS);
	assert(self->clusters[cluster].len.present &&
	       index < self->clusters[cluster].len.value);
	entry_t *entry =
		((entry_t *)((uint8_t *)self + self->clusters[cluster].offset)) + index;
	reserved_t old;
	return atom_update(&entry->preferred, old, ll_reserved_inc, tree_idx,
			   frames);
}

local_result_t ll_local_set_start(local_t *self, uint8_t cluster, size_t index,
				  row_id_t start_row)
{
	assert(cluster < LLFREE_MAX_CLUSTERS);
	assert(self->clusters[cluster].len.present &&
	       index < self->clusters[cluster].len.value);
	entry_t *entry =
		((entry_t *)((uint8_t *)self + self->clusters[cluster].offset)) + index;
	reserved_t old;
	bool ok = atom_update(&entry->preferred, old, ll_reserved_set_start,
			      start_row);
	return make_result(ok, cluster, old);
}

local_result_t ll_local_swap(local_t *self, uint8_t cluster, size_t index,
			     tree_id_t new_tree_idx, treeF_t new_free)
{
	assert(cluster < LLFREE_MAX_CLUSTERS);
	assert(self->clusters[cluster].len.present &&
	       index < self->clusters[cluster].len.value);
	entry_t *entry =
		((entry_t *)((uint8_t *)self + self->clusters[cluster].offset)) + index;
	reserved_t new =
		ll_reserved_new(true, new_free, row_from_tree(new_tree_idx));
	reserved_t old;
	atom_update(&entry->preferred, old, ll_reserved_swap, new);
	return make_result(true, cluster, old);
}

/// Steal frames from a slot where the policy allows Match or Steal.
/// Iterates cluster-by-cluster, starting from the requested cluster
local_result_t ll_local_steal(local_t *self, uint8_t cluster, size_t index,
			      tree_id_optional_t tree_idx, treeF_t frames,
			      llfree_policy_fn policy)
{
	for (size_t i = 0; i < LLFREE_MAX_CLUSTERS; i++) {
		uint8_t target_cluster = (uint8_t)((i + cluster) % LLFREE_MAX_CLUSTERS);
		cluster_locals_t *target = &self->clusters[target_cluster];
		if (!target->len.present || target->len.value == 0)
			continue;

		llfree_policy_t p = policy(cluster, target_cluster, frames);
		if (p.type != LLFREE_POLICY_MATCH &&
		    p.type != LLFREE_POLICY_STEAL)
			continue;

		for (size_t j = 0; j < target->len.value; j++) {
			size_t jj = (index + j) % target->len.value;
			reserved_t old;
			entry_t *target_entries =
				(entry_t *)((uint8_t *)self + target->offset);
			bool ok = atom_update(&target_entries[jj].preferred, old,
					      ll_reserved_dec, tree_idx,
					      frames);
			if (ok)
				return make_result(true, target_cluster, old);
		}
	}
	return (local_result_t){ .success = false };
}

/// Find a slot where the policy returns Demote, atomically take it, and
/// swap the decremented tree into the requesting local.
demote_any_result_t ll_local_demote_any(local_t *self, uint8_t cluster,
					ll_optional_t index,
					tree_id_optional_t tree_idx,
					treeF_t frames, llfree_policy_fn policy)
{
	demote_any_result_t fail = { .found = false };

	for (uint8_t i = 1; i < LLFREE_MAX_CLUSTERS; i++) {
		uint8_t target_cluster = (uint8_t)((i + cluster) % LLFREE_MAX_CLUSTERS);
		cluster_locals_t *target = &self->clusters[target_cluster];
		if (!target->len.present || target->len.value == 0)
			continue;

		llfree_policy_t p = policy(cluster, target_cluster, frames);
		if (p.type != LLFREE_POLICY_DEMOTE)
			continue;

		for (size_t j = 0; j < target->len.value; j++) {
			size_t idx = index.present ? index.value : 0;
			size_t jj = (idx + j) % target->len.value;

			// Atomically take the slot if present
			reserved_t old;
			entry_t *target_entries =
				(entry_t *)((uint8_t *)self + target->offset);
			if (!atom_update(&target_entries[jj].preferred, old,
					 ll_reserved_take, tree_idx, frames))
				continue;

			reserved_t new_res = old;
			bool success =
				ll_reserved_dec(&new_res, tree_idx, frames);
			assert(success); // we just took it, so this should not fail

			// Swap into the requesting local
			cluster_locals_t *req = &self->clusters[cluster];
			if (index.present && req->len.present &&
			    req->len.value > 0 && idx < req->len.value) {
				entry_t *req_entries =
					(entry_t *)((uint8_t *)self + req->offset);
				reserved_t prev;
				atom_update(&req_entries[idx].preferred, prev,
					    ll_reserved_swap, new_res);
				// Return the previous reservation for unreservation, if present
				return (demote_any_result_t){
					.found = true,
					.row = row_id(new_res.start_row),
					.unreserve = prev.present,
					.unres_row = row_id(prev.start_row),
					.unres_cluster = cluster,
					.unres_free = prev.free,
				};
			}
			// Or return (and unreserve) the demoted tree if no local reservation
			return (demote_any_result_t){
				.found = true,
				.row = row_id(new_res.start_row),
				.unreserve = true,
				.unres_row = row_id(new_res.start_row),
				.unres_cluster = cluster,
				.unres_free = new_res.free,
			};
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
		self->frees = 0;
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

bool ll_local_free_inc(local_t *self, uint8_t cluster, size_t index,
		       tree_id_t tree_idx)
{
#if LLFREE_ENABLE_FREE_RESERVE
	assert(cluster < LLFREE_MAX_CLUSTERS);
	assert(index < self->clusters[cluster].len);
	entry_t *entry =
		((entry_t *)((uint8_t *)self + self->clusters[cluster].offset)) + index;
	local_history_t frees;
	bool updated = atom_update(&entry->last, frees, frees_inc, tree_idx);
	// !updated means threshold was reached → caller should reserve
	return !updated;
#else
	(void)self;
	(void)cluster;
	(void)index;
	(void)tree_idx;
	return false;
#endif
}

local_result_t ll_local_drain(local_t *self, uint8_t cluster, size_t index)
{
	assert(cluster < LLFREE_MAX_CLUSTERS);
	assert(self->clusters[cluster].len.present &&
	       index < self->clusters[cluster].len.value);
	entry_t *entry =
		((entry_t *)((uint8_t *)self + self->clusters[cluster].offset)) + index;
	reserved_t old;
	atom_update(&entry->preferred, old, ll_reserved_swap,
		    ll_reserved_new(false, 0, row_id(0)));
	return make_result(old.present, cluster, old);
}

ll_tree_stats_t ll_local_stats(const local_t *self)
{
	ll_tree_stats_t stats = { 0 };
	for (uint8_t t = 0; t < LLFREE_MAX_CLUSTERS; t++) {
		const cluster_locals_t *tl = &self->clusters[t];
		for (size_t j = 0; tl->len.present && j < tl->len.value; j++) {
			entry_t *entries =
				(entry_t *)((uint8_t *)self + tl->offset);
			reserved_t res = atom_load(&entries[j].preferred);
			if (res.present) {
				stats.free_frames += res.free;
				stats.free_trees += res.free ==
						    LLFREE_TREE_SIZE;
				stats.clusters[t].free_frames += res.free;
			}
		}
	}
	return stats;
}

local_result_t ll_local_stats_at(const local_t *self, tree_id_t tree_idx)
{
	for (uint8_t t = 0; t < LLFREE_MAX_CLUSTERS; t++) {
		const cluster_locals_t *tl = &self->clusters[t];
		for (size_t j = 0; tl->len.present && j < tl->len.value; j++) {
			entry_t *entries =
				(entry_t *)((uint8_t *)self + tl->offset);
			reserved_t res = atom_load(&entries[j].preferred);
			if (!res.present)
				continue;
			if (tree_from_row(row_id(res.start_row)).value ==
			    tree_idx.value)
				return make_result(true, t, res);
		}
	}
	return (local_result_t){ .success = false,
				 .present = false,
				 .free = 0,
				 .start_row = row_id(0) };
}

void ll_local_print(const local_t *self, size_t indent)
{
	if (indent == 0)
		llfree_info_start();
	llfree_info_cont("%sll_local_t {\n", INDENT(indent));
	size_t total = 0;
	for (size_t i = 0; i < self->num_clusters; i++)
		total += self->clusters[i].len.value;
	llfree_info_cont("%snum_clusters: %u, total: %zu\n", INDENT(indent + 1),
			 self->num_clusters, total);

	for (uint8_t t = 0; t < LLFREE_MAX_CLUSTERS; t++) {
		const cluster_locals_t *tl = &self->clusters[t];
		if (!tl->len.present || tl->len.value == 0)
			continue;
		llfree_info_cont("%scluster %u (%zu entries):\n",
				 INDENT(indent + 1), t, tl->len.value);
		for (size_t j = 0; j < tl->len.value; j++) {
			entry_t *entries =
				(entry_t *)((uint8_t *)self + tl->offset);
			reserved_t res = atom_load(&entries[j].preferred);
			llfree_info_cont(
				"%s[%zu] { present: %d, free: %" PRIu64
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
	for (uint8_t t = 0; t < LLFREE_MAX_CLUSTERS; t++) {
		const cluster_locals_t *tl = &self->clusters[t];
		for (size_t j = 0; tl->len.present && j < tl->len.value; j++) {
			entry_t *entries =
				(entry_t *)((uint8_t *)self + tl->offset);
			reserved_t res = atom_load(&entries[j].preferred);
			assert(res.free <= LLFREE_TREE_SIZE);
			if (res.present) {
				validate_tree(llfree,
					      make_result(true, t, res));
			}
		}
	}
}
