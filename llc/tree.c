#include "tree.h"
#include "utils.h"

tree_t tree_new(uint16_t counter, bool flag)
{
	assert(counter <= TREESIZE); // max limit for 15 bit
	tree_t ret;
	ret.flag = flag;
	ret.counter = counter;
	return ret;
}

bool tree_reserve(tree_t *self, _void v)
{
	(void)v;
	assert(self != NULL);

	// tree is already reserved
	if (self->flag)
		return false;
	*self = tree_new(0, true);

	return true;
}

bool tree_steal_counter(tree_t *self, _void v)
{
	(void)v;
	assert(self != NULL);

	*self = tree_new(0, true);
	return true;
}

bool tree_writeback(tree_t *self, uint16_t free_counter)
{
	assert(self != NULL);
	assert(free_counter + self->counter <= TREESIZE);

	*self = tree_new(free_counter + self->counter, false);
	return true;
}

saturation_level_t tree_status(const tree_t self)
{
	const size_t lower_limit = 2 << HP_ORDER;
	const size_t upper_limit = TREESIZE - (8 << HP_ORDER);

	if (self.counter < lower_limit || self.flag)
		return ALLOCATED; // reserved trees will be counted as allocated
	if (self.counter > upper_limit)
		return FREE;
	return PARTIAL;
}

bool tree_counter_inc(tree_t *self, size_t order)
{
	assert(self != NULL);
	assert(self->counter + (1 << order) <= TREESIZE);

	self->counter += 1 << order;
	return true;
}

bool tree_counter_dec(tree_t *self, size_t order)
{
	assert(self != NULL);
	assert(self->counter >= 1 << order);

	self->counter -= 1 << order;
	return true;
}
