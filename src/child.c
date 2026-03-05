#include "child.h"

bool child_inc(child_t *self, size_t order)
{
	uint16_t num_pages = (uint16_t)(1u << order);
	if (self->huge == true ||
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

bool child_set_huge(child_t *self)
{
	if (self->free == LLFREE_CHILD_SIZE) {
		assert(!self->huge);
		*self = child_huge();
		return true;
	}
	return false;
}

bool child_clear_huge(child_t *self)
{
	if (self->huge) {
		assert(self->free == 0);
		*self = child_new(LLFREE_CHILD_SIZE);
		return true;
	}
	return false;
}

bool child_set_max(child_pair_t *self)
{
	if (self->first.free == LLFREE_CHILD_SIZE &&
	    self->second.free == LLFREE_CHILD_SIZE) {
		assert(!self->first.huge && !self->second.huge);
		self->first = child_huge();
		self->second = child_huge();
		return true;
	}
	return false;
}

bool child_clear_max(child_pair_t *self)
{
	if (self->first.huge && self->second.huge) {
		assert(self->first.free == 0 && self->second.free == 0);
		self->first = child_new(LLFREE_CHILD_SIZE);
		self->second = child_new(LLFREE_CHILD_SIZE);
		return true;
	}
	return false;
}
