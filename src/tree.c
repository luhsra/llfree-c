#include "tree.h"
#include "llfree_platform.h"

bool tree_put(tree_t *self, treeF_t frames, uint8_t default_tier)
{
	treeF_t free = self->free + frames;
	assert(free <= LLFREE_TREE_SIZE);
	self->free = free;
	if (free == LLFREE_TREE_SIZE)
		self->tier = default_tier;
	return true;
}

bool tree_get_match(tree_t *self, uint8_t tier, treeF_t frames,
		    llfree_policy_fn policy)
{
	if (self->free < frames)
		return false;
	llfree_policy_t p = policy(tier, self->tier, self->free);
	if (p.type != LLFREE_POLICY_MATCH)
		return false;
	self->free -= frames;
	return true;
}

bool tree_get_demote(tree_t *self, uint8_t tier, treeF_t frames,
		     llfree_policy_fn policy)
{
	if (self->free < frames)
		return false;
	llfree_policy_t p = policy(tier, self->tier, self->free);
	switch (p.type) {
	case LLFREE_POLICY_MATCH:
	case LLFREE_POLICY_STEAL:
		self->free -= frames;
		return true;
	case LLFREE_POLICY_DEMOTE:
		self->tier = tier;
		self->free -= frames;
		return true;
	default:
		return false;
	}
}

bool tree_get_demote_only(tree_t *self, uint8_t tier, treeF_t frames,
			  llfree_policy_fn policy)
{
	if (self->free < frames)
		return false;
	llfree_policy_t p = policy(tier, self->tier, self->free);
	if (p.type != LLFREE_POLICY_DEMOTE)
		return false;
	self->tier = tier;
	self->free -= frames;
	return true;
}

bool tree_reserve(tree_t *self, uint8_t tier, treeF_t min, treeF_t max)
{
	if (self->reserved)
		return false;
	if (self->free < min || self->free > max)
		return false;
	/* tier must match or tree must be entirely free */
	if (self->tier != tier && self->free != LLFREE_TREE_SIZE)
		return false;
	self->tier = tier;
	self->reserved = true;
	self->free = 0;
	return true;
}

bool tree_reserve_demote(tree_t *self, uint8_t tier, treeF_t min,
			 llfree_policy_fn policy)
{
	if (self->reserved)
		return false;
	if (self->free < min)
		return false;
	llfree_policy_t p = policy(tier, self->tier, self->free);
	if (p.type != LLFREE_POLICY_DEMOTE)
		return false;
	self->tier = tier;
	self->reserved = true;
	self->free = 0;
	return true;
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
	treeF_t free = self->free + frames;
	assert(free <= LLFREE_TREE_SIZE);
	self->free = free;
	if (free == LLFREE_TREE_SIZE)
		self->tier = default_tier;
	return true;
}

bool tree_sync_steal(tree_t *self, treeF_t min)
{
	if (self->reserved && self->free > min) {
		self->free = 0;
		return true;
	}
	return false;
}

bool tree_put_or_reserve(tree_t *self, treeF_t frames, uint8_t tier,
			 bool *reserve, treeF_t min, uint8_t default_tier)
{
	treeF_t free = self->free + frames;
	assert(free <= LLFREE_TREE_SIZE);
	self->free = free;
	if (free == LLFREE_TREE_SIZE)
		self->tier = default_tier;

	if (*reserve && !self->reserved && self->tier == tier &&
	    self->free > min) {
		*reserve = true;
		self->free = 0;
		self->reserved = true;
	} else {
		*reserve = false;
	}
	return true;
}

void tree_print(tree_t *self, size_t idx, size_t indent)
{
	if (indent == 0)
		llfree_info_start();
	llfree_info_cont("%stree[%" PRIuS
			 "] { reserved: %d, tier: %u, free: %" PRIuS " }\n",
			 INDENT(indent), idx, self->reserved,
			 (unsigned)self->tier, (size_t)self->free);
	if (indent == 0)
		llfree_info_end();
}
