#include "child.h"

bool child_inc(child_t *self, size_t order)
{
	size_t num_pages = 1 << order;
	if (self->huge == true || self->free + num_pages > LLFREE_CHILD_SIZE)
		return false;

	self->free += num_pages;
	return true;
}

bool child_dec(child_t *self, size_t order)
{
	size_t num_pages = 1 << order;
	if (self->huge || self->free < num_pages)
		return false;

	self->free -= num_pages;
	return true;
}

bool child_reserve_huge(child_t *self)
{
	if (self->free != LLFREE_CHILD_SIZE)
		return false;

	*self = child_new(0, true, false);
	return true;
}

bool child_reserve_max(child_pair_t *self)
{
	if (self->first.free != LLFREE_CHILD_SIZE ||
	    self->second.free != LLFREE_CHILD_SIZE)
		return false;

	self->first = child_new(0, true, false);
	self->second = child_new(0, true, false);
	return true;
}

bool child_inflate(child_t *self) {
	if (self->free == LLFREE_CHILD_SIZE && !self->inflated) {
		*self = child_new(self->free, false, true);
		return true;
	}
	return false;
}

bool child_deflate(child_t *self) {
	if (self->inflated) {
		self->inflated = false;
		return true;
	}
	return false;
}
