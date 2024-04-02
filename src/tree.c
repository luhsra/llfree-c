#include "tree.h"

bool tree_reserve(tree_t *self, size_t min, size_t max, int kind)
{
	assert(min < max);

	if (!self->reserved && min <= self->free && self->free <= max &&
	    (self->kind == kind || self->free == LLFREE_TREE_SIZE)) {
		*self = tree_new(0, true, kind);
		return true;
	}
	return false;
}

bool tree_steal_counter(tree_t *self, size_t min)
{
	if (self->reserved && self->free >= min) {
		*self = tree_new(0, true, self->kind);
		return true;
	}
	return false;
}

bool tree_writeback(tree_t *self, size_t free, int kind)
{
	size_t f = self->free + free;
	assert(f <= LLFREE_TREE_SIZE);
	assert(self->reserved);

	*self = tree_new(f, false, kind);
	return true;
}

bool tree_inc(tree_t *self, size_t free)
{
	assert(self->free + free <= LLFREE_TREE_SIZE);

	self->free += free;
	return true;
}

bool tree_inc_or_reserve(tree_t *self, size_t free, bool *reserve, size_t min,
			 size_t max)
{
	tree_inc(self, free); // update counter

	if (reserve && *reserve) // reserve if needed
		*reserve = tree_reserve(self, min, max, self->kind);
	return true;
}
