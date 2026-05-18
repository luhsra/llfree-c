#include "child.h"

bool child_inc(child_t *self, size_t order)
{
	uint16_t num_pages = (uint16_t)(1u << order);
	if (self->huge ||
	    (uint16_t)(self->free + num_pages) > LLFREE_CHILD_SIZE)
		return false;

	self->free += num_pages;
	return true;
}

bool child_dec(child_t *self, size_t order)
{
	uint16_t num_pages = (uint16_t)(1u << order);
	if (!self->huge && self->free >= num_pages) {
		self->free -= num_pages;
		return true;
	}
	return false;
}
