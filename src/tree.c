#include "tree.h"
#include "llfree.h"
#include "llfree_platform.h"
#include "llfree_types.h"

bool tree_put(tree_t *self, treeF_t frames, llfree_policy_fn policy,
	      uint8_t default_tier)
{
	treeF_t free = self->free + frames;
	assert(free <= LLFREE_TREE_SIZE);
	self->free = free;
	if (free == LLFREE_TREE_SIZE &&
	    policy(self->tier, default_tier, self->free).type !=
		    LLFREE_POLICY_INVALID)
		self->tier = default_tier;
	return true;
}

bool tree_steal(tree_t *self, treeF_t frames, uint8_t *tier,
		llfree_policy_fn policy)
{
        assert(tier != NULL);
	if (self->free < frames)
		return false;

	llfree_policy_t p = policy(*tier, self->tier, self->free);
	uint8_t new_tier = LLFREE_TIER_NONE;
	switch (p.type) {
	case LLFREE_POLICY_MATCH:
	case LLFREE_POLICY_DEMOTE:
		new_tier = *tier; // demote to the tier of the stealer
		break;
	case LLFREE_POLICY_STEAL:
		new_tier = self->tier; // keep the current tier
		*tier = self->tier;
		break;
	default:
		return false;
	}

	self->free -= frames;
	self->tier = new_tier;
	return true;
}

bool tree_reserve_or_steal(tree_t *self, treeF_t frames,
			   llfree_policy_fn policy, uint8_t tier,
			   bool *out_reserved, uint8_t *out_tier)
{
	if (self->reserved || self->free < frames)
		return false;
	llfree_policy_t p = policy(tier, self->tier, self->free);
	switch (p.type) {
	case LLFREE_POLICY_MATCH:
	case LLFREE_POLICY_DEMOTE:
		// Reserve: take all free frames, mark reserved
		*out_reserved = true;
		*out_tier = tier;
		self->reserved = true;
		self->free = 0;
		self->tier = tier;
		return true;
	case LLFREE_POLICY_STEAL:
		// Steal: just decrement counter, keep tier
		*out_reserved = false;
		*out_tier = self->tier;
		self->free -= frames;
		return true;
	default:
		return false;
	}
}

bool tree_unreserve_add(tree_t *self, treeF_t frames, uint8_t tier,
			llfree_policy_fn policy, uint8_t default_tier)
{
	if (!self->reserved)
		return false;
	self->reserved = false;
	llfree_policy_t p = policy(tier, self->tier, frames);
	if (p.type == LLFREE_POLICY_DEMOTE)
		self->tier = tier;
	return tree_put(self, frames, policy, default_tier);
}

bool tree_sync_steal(tree_t *self, treeF_t min)
{
	if (self->reserved && self->free > min) {
		self->free = 0;
		return true;
	}
	return false;
}

bool tree_change(tree_t *self, uint8_t match_tier, treeF_t min_free,
		 uint8_t change_tier, llfree_tree_operation_t operation,
		 treeF_t online_free)
{
	if (self->reserved)
		return false;
	if (match_tier != LLFREE_TIER_NONE && self->tier != match_tier)
		return false;
	if (self->free < min_free)
		return false;

	if (change_tier != LLFREE_TIER_NONE)
		self->tier = change_tier;

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
			 "] { reserved: %d, tier: %u, free: %" PRIuS " }\n",
			 INDENT(indent), idx.value, self->reserved,
			 (unsigned)self->tier, (size_t)self->free);
	if (indent == 0)
		llfree_info_end();
}
