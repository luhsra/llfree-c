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

bool tree_steal_counter(tree_t *self)
{
	*self = tree_new(0, true);
	return true;
}

bool tree_writeback(tree_t *self, uint16_t free)
{
	if (self->free + free <= TREESIZE) {
		*self = tree_new(free + self->free, false);
		return true;
	}
	return false;
}

bool tree_inc(tree_t *self, size_t free)
{
	if (self->free + free <= TREESIZE) {
		self->free += free;
		return true;
	}
	return false;
}
