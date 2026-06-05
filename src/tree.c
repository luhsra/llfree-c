#include "tree.h"
#include "llfree.h"
#include "llfree_platform.h"
#include "llfree_types.h"

bool tree_put(tree_t *self, treeF_t frames, llfree_policy_fn policy,
	      uint8_t default_cluster)
{
	treeF_t free = self->free + frames;
	assert(free <= LLFREE_TREE_SIZE);
	self->free = free;
	if (free == LLFREE_TREE_SIZE &&
	    policy(self->cluster, default_cluster, self->free).type !=
		    LLFREE_POLICY_INVALID)
		self->cluster = default_cluster;
	return true;
}

bool tree_steal(tree_t *self, treeF_t frames, uint8_t *cluster,
		llfree_policy_fn policy)
{
        assert(cluster != NULL);
	if (self->free < frames)
		return false;

	llfree_policy_t p = policy(*cluster, self->cluster, self->free);
	uint8_t new_cluster = LLFREE_CLUSTER_NONE;
	switch (p.type) {
	case LLFREE_POLICY_MATCH:
	case LLFREE_POLICY_DEMOTE:
		new_cluster = *cluster; // demote to the cluster of the stealer
		break;
	case LLFREE_POLICY_STEAL:
		new_cluster = self->cluster; // keep the current cluster
		*cluster = self->cluster;
		break;
	default:
		return false;
	}

	self->free -= frames;
	self->cluster = new_cluster;
	return true;
}

bool tree_reserve_or_steal(tree_t *self, treeF_t frames,
			   llfree_policy_fn policy, uint8_t cluster,
			   bool *out_reserved, uint8_t *out_cluster)
{
	if (self->reserved || self->free < frames)
		return false;
	llfree_policy_t p = policy(cluster, self->cluster, self->free);
	switch (p.type) {
	case LLFREE_POLICY_MATCH:
	case LLFREE_POLICY_DEMOTE:
		// Reserve: take all free frames, mark reserved
		*out_reserved = true;
		*out_cluster = cluster;
		self->reserved = true;
		self->free = 0;
		self->cluster = cluster;
		return true;
	case LLFREE_POLICY_STEAL:
		// Steal: just decrement counter, keep cluster
		*out_reserved = false;
		*out_cluster = self->cluster;
		self->free -= frames;
		return true;
	default:
		return false;
	}
}

bool tree_unreserve_add(tree_t *self, treeF_t frames, uint8_t cluster,
			llfree_policy_fn policy, uint8_t default_cluster)
{
	if (!self->reserved)
		return false;
	self->reserved = false;
	llfree_policy_t p = policy(cluster, self->cluster, frames);
	if (p.type == LLFREE_POLICY_DEMOTE)
		self->cluster = cluster;
	return tree_put(self, frames, policy, default_cluster);
}

bool tree_sync_steal(tree_t *self, treeF_t min)
{
	if (self->reserved && self->free > min) {
		self->free = 0;
		return true;
	}
	return false;
}

bool tree_change(tree_t *self, uint8_t match_cluster, treeF_t min_free,
		 uint8_t change_cluster, llfree_tree_operation_t operation,
		 treeF_t online_free)
{
	if (self->reserved)
		return false;
	if (match_cluster != LLFREE_CLUSTER_NONE && self->cluster != match_cluster)
		return false;
	if (self->free < min_free)
		return false;

	if (change_cluster != LLFREE_CLUSTER_NONE)
		self->cluster = change_cluster;

	switch (operation) {
	case LLFREE_TREE_OP_NONE:
		break;
	case LLFREE_TREE_OP_OFFLINE:
		self->free = 0;
		break;
	case LLFREE_TREE_OP_ONLINE:
		if (self->free != 0)
			return false;
		self->free = online_free;
		break;
	}

	return true;
}

void tree_print(tree_t *self, tree_id_t idx, size_t indent)
{
	if (indent == 0)
		llfree_info_start();
	llfree_info_cont("%stree[%" PRIuS
			 "] { reserved: %d, cluster: %u, free: %" PRIuS " }\n",
			 INDENT(indent), idx.value, self->reserved,
			 (unsigned)self->cluster, (size_t)self->free);
	if (indent == 0)
		llfree_info_end();
}
