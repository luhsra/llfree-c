#include "tree.h"
#include "utils.h"
#include <assert.h>

bool tree_reserve(tree_t *self, size_t min, size_t max)
{
	assert(min < max);

	// tree is already reserved
	if (self->flag || !(min <= self->free && self->free <= max))
		return false;

	*self = tree_new(0, true);
	return true;
}

bool tree_steal_counter(tree_t *self)
{
	*self = tree_new(0, true);
	return true;
}

bool tree_writeback(tree_t *self, uint16_t free_counter)
{
	if (free_counter + self->free <= TREESIZE) {
		*self = tree_new(free_counter + self->free, false);
		return true;
	}
	return false;
}

bool tree_inc(tree_t *self, size_t order)
{
	if (self->free + (1 << order) <= TREESIZE) {
		self->free += 1 << order;
		return true;
	}
	return false;
}

bool tree_dec(tree_t *self, size_t order)
{
	if (self->free >= 1 << order) {
		self->free -= 1 << order;
		return true;
	}
	return false;
}
