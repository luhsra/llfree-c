#include "tree.h"
#include "llfree.h"
#include "llfree_platform.h"
#include "llfree_types.h"

bool tree_put(tree_t *self, treeF_t frames, llfree_policy_fn policy,
	      uint8_t default_class)
{
	treeF_t free = self->free + frames;
	assert(free <= LLFREE_TREE_SIZE);
	self->free = free;
	if (free == LLFREE_TREE_SIZE &&
	    policy(self->class, default_class, self->free).type !=
		    LLFREE_POLICY_INVALID)
		self->class = default_class;
	return true;
}

bool tree_steal(tree_t *self, treeF_t frames, uint8_t *class,
		llfree_policy_fn policy)
{
	assert(class != NULL);
	if (self->free < frames)
		return false;

	llfree_policy_t p = policy(*class, self->class, self->free);
	uint8_t new_class = LLFREE_CLASS_NONE;
	switch (p.type) {
	case LLFREE_POLICY_MATCH:
	case LLFREE_POLICY_DEMOTE:
		new_class = *class; // demote to the class of the stealer
		break;
	case LLFREE_POLICY_STEAL:
		new_class = self->class; // keep the current class
		*class = self->class;
		break;
	default:
		return false;
	}

	self->free -= frames;
	self->class = new_class;
	return true;
}

bool tree_reserve_or_steal(tree_t *self, treeF_t frames,
			   llfree_policy_fn policy, uint8_t class,
			   bool *out_reserved, uint8_t *out_class)
{
	if (self->reserved || self->free < frames)
		return false;
	llfree_policy_t p = policy(class, self->class, self->free);
	switch (p.type) {
	case LLFREE_POLICY_MATCH:
	case LLFREE_POLICY_DEMOTE:
		// Reserve: take all free frames, mark reserved
		*out_reserved = true;
		*out_class = class;
		self->reserved = true;
		self->free = 0;
		self->class = class;
		return true;
	case LLFREE_POLICY_STEAL:
		// Steal: just decrement counter, keep class
		*out_reserved = false;
		*out_class = self->class;
		self->free -= frames;
		return true;
	default:
		return false;
	}
}

bool tree_unreserve_add(tree_t *self, treeF_t frames, uint8_t class,
			llfree_policy_fn policy, uint8_t default_class)
{
	if (!self->reserved)
		return false;
	self->reserved = false;
	llfree_policy_t p = policy(class, self->class, frames);
	if (p.type == LLFREE_POLICY_DEMOTE)
		self->class = class;
	return tree_put(self, frames, policy, default_class);
}

bool tree_sync_steal(tree_t *self, treeF_t min)
{
	if (self->reserved && self->free > min) {
		self->free = 0;
		return true;
	}
	return false;
}

bool tree_change(tree_t *self, uint8_t match_class, treeF_t min_free,
		 uint8_t change_class, llfree_tree_operation_t operation,
		 treeF_t online_free)
{
	if (self->reserved)
		return false;
	if (match_class != LLFREE_CLASS_NONE && self->class != match_class)
		return false;
	if (self->free < min_free)
		return false;

	if (change_class != LLFREE_CLASS_NONE)
		self->class = change_class;

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
			 "] { reserved: %d, class: %u, free: %" PRIuS " }\n",
			 INDENT(indent), idx.value, self->reserved,
			 (unsigned)self->class, (size_t)self->free);
	if (indent == 0)
		llfree_info_end();
}
