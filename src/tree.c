#include "tree.h"

bool tree_reserve(tree_t *self, size_t min, size_t max, size_t span, bool movable)
{
	assert(min < max);

	if (!self->reserved && min <= self->free && self->free <= max &&
	    (self->movable == movable || self->free == span)) {
		*self = tree_new(0, true, movable);
		return true;
	}
	return false;
}

bool tree_steal_counter(tree_t *self, size_t min)
{
	if (self->reserved && self->free >= min) {
		*self = tree_new(0, true, self->movable);
		return true;
	}
	return false;
}

bool tree_writeback(tree_t *self, size_t free, bool movable)
{
	size_t f = self->free + free;
	assert(f <= LLFREE_TREE_SIZE);
	assert(self->reserved);

	*self = tree_new(f, false, movable);
	return true;
}

bool tree_inc(tree_t *self, size_t free)
{
	assert(self->free + free <= LLFREE_TREE_SIZE);

	self->free += free;
	return true;
}

bool tree_inc_or_reserve(tree_t *self, size_t free, bool *reserve, size_t min,
			 size_t max, size_t span)
{
	tree_inc(self, free); // update counter

	if (reserve && *reserve) // reserve if needed
		*reserve = tree_reserve(self, min, max, span, self->movable);
	return true;
}
