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

bool child_set_huge(child_t *self)
{
	if (self->free == LLFREE_CHILD_SIZE) {
		assert(!self->huge);
		*self = child_new(0, true, self->inflated);
		return true;
	}
	return false;
}

bool child_clear_huge(child_t *self)
{
	if (self->huge) {
		assert(self->free == 0);
		*self = child_new(LLFREE_CHILD_SIZE, false, self->inflated);
		return true;
	}
	return false;
}

bool child_set_max(child_pair_t *self)
{
	if (self->first.free == LLFREE_CHILD_SIZE &&
	    self->second.free == LLFREE_CHILD_SIZE) {
		assert(!self->first.huge && !self->second.huge);
		self->first = child_new(0, true, self->first.inflated);
		self->second = child_new(0, true, self->second.inflated);
		return true;
	}
	return false;
}

bool child_clear_max(child_pair_t *self)
{
	if (self->first.huge && self->second.huge) {
		assert(self->first.free == 0 && self->second.free == 0);
		self->first = child_new(LLFREE_CHILD_SIZE, false,
					self->first.inflated);
		self->second = child_new(LLFREE_CHILD_SIZE, false,
					 self->second.inflated);
		return true;
	}
	return false;
}

bool child_inflate(child_t *self, bool alloc)
{
	if (self->free == LLFREE_CHILD_SIZE && !self->huge &&
	    (alloc ? true : !self->inflated)) {
		*self = child_new(alloc ? 0 : LLFREE_CHILD_SIZE, alloc, true);
		return true;
	}
	return false;
}

bool child_inflate_put(child_t *self)
{
	if (self->inflated && self->huge) {
		assert(self->free == 0);
		*self = child_new(LLFREE_CHILD_SIZE, false, true);
		return true;
	}
	return false;
}

bool child_deflate(child_t *self)
{
	if (self->inflated) {
		self->inflated = false;
		return true;
	}
	return false;
}
