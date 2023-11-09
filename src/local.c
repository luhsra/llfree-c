#include "local.h"

void local_init(local_t *self)
{
	assert(self != NULL);
	atom_store(&self->last_free, ((last_free_t){ 0, 0 }));
	atom_store(&self->reserved, ((reserved_t){ 0, 0, false, false }));
}

bool reserved_swap(reserved_t *self, reserved_t new, bool expect_reserving)
{
	assert(self != NULL);
	if (self->reserving == expect_reserving) {
		*self = new;
		return true;
	}
	return false;
}

bool reserved_set_start(reserved_t *self, size_t tree_idx)
{
	// no update if reservation is in progress
	if (!self->reserving) {
		self->start_row = row_from_tree(tree_idx);
		return true;
	}
	return false;
}

bool reserved_set_reserving(reserved_t *self, bool reserving)
{
	// only update if reserving is different
	if (self->reserving != reserving) {
		self->reserving = reserving;
		return true;
	}
	return false;
}

bool reserved_inc(reserved_t *self, size_t tree_idx, size_t free)
{
	// check if reserved tree is a match for given pfn
	if (!self->present || self->reserving ||
	    tree_from_row(self->start_row) != tree_idx)
		return false;

	// check if counter has enough space
	assert(self->free + free <= LLFREE_TREE_SIZE);
	self->free += free;
	return true;
}

bool reserved_dec(reserved_t *self, size_t free)
{
	if (!self->present || self->reserving || self->free < free) {
		// not enough free frames in this tree
		return false;
	}

	self->free -= free;
	return true;
}

bool last_free_inc(last_free_t *self, uint64_t tree_idx)
{
	assert(self != NULL);

	// if last free was in another tree -> overwrite last reserved Index
	if (self->tree_idx != tree_idx) {
		self->tree_idx = tree_idx;
		self->counter = 0;
	} else if (self->counter < 3) {
		// if the same tree -> increase the counter for this
		self->counter += 1;
	}

	return true;
}
