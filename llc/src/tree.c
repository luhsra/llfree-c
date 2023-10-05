#include "tree.h"
#include "utils.h"
#include <assert.h>

bool tree_reserve(tree_t *self, range_t free)
{
	assert(free.min < free.max);

	// tree is already reserved
	if (self->flag ||
	    !(free.min <= self->counter && self->counter <= free.max))
		return false;

	*self = tree_new(0, true);
	return true;
}

bool tree_steal_counter(tree_t *self, _void _unused v)
{
	*self = tree_new(0, true);
	return true;
}

bool tree_writeback(tree_t *self, uint16_t free_counter)
{
	assert(free_counter + self->counter <= TREESIZE);
	*self = tree_new(free_counter + self->counter, false);
	return true;
}

bool tree_counter_inc(tree_t *self, size_t order)
{
	assert(self->counter + (1 << order) <= TREESIZE);
	self->counter += 1 << order;
	return true;
}

bool tree_counter_dec(tree_t *self, size_t order)
{
	assert(self->counter >= 1 << order);
	self->counter -= 1 << order;
	return true;
}
