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
	self->reported = false;
	return true;
}

bool child_reserve_huge(child_t *self, llflags_t flags)
{
	if (self->free != LLFREE_CHILD_SIZE)
		return false;

	if (flags.get_unreported && self->reported)
		return false;

	*self = child_new(0, true, false);
	return true;
}

bool child_reserve_max(child_pair_t *self, llflags_t flags)
{
	if (self->first.free != LLFREE_CHILD_SIZE ||
	    self->second.free != LLFREE_CHILD_SIZE)
		return false;

	if (flags.get_unreported &&
	    (self->first.reported || self->second.reported))
		return false;

	self->first = child_new(0, true, false);
	self->second = child_new(0, true, false);
	return true;
}
