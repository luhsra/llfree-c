#include "tree.h"

bool tree_reserve(tree_t *self, size_t min, size_t max)
{
	assert(min < max);

	// tree is already reserved
	if (self->reserved || !(min <= self->free && self->free <= max))
		return false;

	*self = tree_new(0, true);
	return true;
}

bool tree_steal_counter(tree_t *self, size_t min)
{
	if (self->reserved && self->free >= min) {
		*self = tree_new(0, true);
		return true;
	}
	return false;
}

bool tree_writeback(tree_t *self, size_t free)
{
	assert(self->free + free <= LLFREE_TREE_SIZE);

	*self = tree_new(free + self->free, false);
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
		*reserve = tree_reserve(self, min, max);
	return true;
}
