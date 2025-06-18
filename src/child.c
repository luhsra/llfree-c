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

bool child_dec(child_t *self, size_t order, bool allow_reclaimed)
{
	uint16_t num_pages = (uint16_t)(1u << order);
	if (!self->huge && self->free >= num_pages &&
	    (allow_reclaimed || !self->reclaimed)) {
		self->free -= num_pages;
		return true;
	}
	return false;
}

bool child_set_huge(child_t *self, bool allow_reclaimed, bool zeroed)
{
	if (self->free == LLFREE_CHILD_SIZE &&
	    (allow_reclaimed || !self->reclaimed) &&
	    (!zeroed || self->zeroed)) {
		assert(!self->huge);
		*self = child_new(0, true, self->reclaimed, false);
		return true;
	}
	return false;
}

bool child_clear_huge(child_t *self)
{
	if (self->huge) {
		assert(self->free == 0);
		*self = child_new(LLFREE_CHILD_SIZE, false, self->reclaimed,
				  false);
		return true;
	}
	return false;
}

bool child_set_max(child_pair_t *self, bool allow_reclaimed, bool zeroed)
{
	if (self->first.free == LLFREE_CHILD_SIZE &&
	    self->second.free == LLFREE_CHILD_SIZE &&
	    (!zeroed || (self->first.zeroed && self->second.zeroed)) &&
	    (allow_reclaimed ||
	     (!self->first.reclaimed && !self->second.reclaimed))) {
		assert(!self->first.huge && !self->second.huge);
		self->first = child_new(0, true, self->first.reclaimed, false);
		self->second =
			child_new(0, true, self->second.reclaimed, false);
		return true;
	}
	return false;
}

bool child_clear_max(child_pair_t *self)
{
	if (self->first.huge && self->second.huge) {
		assert(self->first.free == 0 && self->second.free == 0);
		self->first = child_new(LLFREE_CHILD_SIZE, false,
					self->first.reclaimed, false);
		self->second = child_new(LLFREE_CHILD_SIZE, false,
					 self->second.reclaimed, false);
		return true;
	}
	return false;
}

bool child_reclaim(child_t *self, bool alloc, bool zeroed)
{
	if (self->free == LLFREE_CHILD_SIZE && !self->huge &&
	    self->zeroed == zeroed && (alloc ? true : !self->reclaimed)) {
		*self = child_new(alloc ? 0 : LLFREE_CHILD_SIZE, alloc, true,
				  self->zeroed);
		return true;
	}
	return false;
}

bool child_return(child_t *self, bool install)
{
	if (self->reclaimed && self->huge) {
		assert(self->free == 0);
		// Returned memory is expected to already be zeroed
		*self = child_new(LLFREE_CHILD_SIZE, false, !install, true);
		return true;
	}
	return false;
}

bool child_install(child_t *self)
{
	if (self->reclaimed) {
		self->reclaimed = false;
		return true;
	}
	return false;
}
