#include "tree.h"

bool tree_reserve(tree_t *self, treeF_t min, treeF_t max, uint8_t kind)
{
	assert(min < max);

	if (!self->reserved && min <= self->free && self->free <= max &&
	    (self->kind == kind || self->free == LLFREE_TREE_SIZE)) {
		*self = tree_new(0, true, kind);
		return true;
	}
	return false;
}

bool tree_steal_counter(tree_t *self, treeF_t min)
{
	if (self->reserved && self->free >= min) {
		*self = tree_new(0, true, self->kind);
		return true;
	}
	return false;
}

bool tree_unreserve(tree_t *self, treeF_t free, uint8_t kind)
{
	treeF_t f = self->free + free;
	assert(f <= LLFREE_TREE_SIZE);
	assert(self->reserved);

	*self = tree_new(f, false, kind);
	return true;
}

bool tree_inc(tree_t *self, treeF_t free)
{
	assert(self->free + free <= LLFREE_TREE_SIZE);

	self->free += free;
	return true;
}

bool tree_dec(tree_t *self, treeF_t free)
{
	if (!self->reserved && self->free >= free) {
		self->free -= free;
		return true;
	}
	return false;
}

bool tree_dec_force(tree_t *self, treeF_t free, uint8_t kind)
{
	// Overwrite tree kind if priority higher (number lower)
	// FIXED > MOVABLE > HUGE
	if (!self->reserved && self->free >= free) {
		if (kind < self->kind)
			self->kind = kind;
		self->free -= free;
		return true;
	}
	return false;
}

bool tree_inc_or_reserve(tree_t *self, treeF_t free, bool *reserve, treeF_t min)
{
	ll_unused bool success = tree_inc(self, free); // update counter
	assert(success);

	if (reserve && *reserve) // reserve if needed
		*reserve =
			tree_reserve(self, min, LLFREE_TREE_SIZE, self->kind);
	return true;
}
