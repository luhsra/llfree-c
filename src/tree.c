#include "tree.h"
#include "llfree_platform.h"
#include "llfree_types.h"
#include <stdbool.h>

bool tree_put(tree_t *self, treeF_t frames, uint8_t default_tier)
{
	treeF_t free = self->free + frames;
	assert(free <= LLFREE_TREE_SIZE);
	self->free = free;
	if (free == LLFREE_TREE_SIZE)
		self->tier = default_tier;
	return true;
}

bool tree_get(tree_t *self, treeF_t frames, uint8_t *result_tier,
	      tree_check_fn check, void *args)
{
	if (self->free < frames)
		return false;
	uint8_t new_tier = check(self->tier, self->free, args);
	if (new_tier == LLFREE_TIER_NONE)
		return false;

	self->free -= frames;
	self->tier = new_tier;
	if (result_tier != NULL)
		*result_tier = new_tier;
	return true;
}

bool tree_reserve(tree_t *self, uint8_t *result_tier, tree_check_fn check,
		  void *args)
{
	assert(check != NULL);
	if (self->reserved)
		return false;
	uint8_t new_tier = check(self->tier, self->free, args);
	if (new_tier == LLFREE_TIER_NONE)
		return false;

	self->reserved = true;
	self->tier = new_tier;
	self->free = 0;
	if (result_tier != NULL)
		*result_tier = new_tier;
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
