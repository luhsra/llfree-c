#include "local.h"

void local_init(local_t *self)
{
	assert(self != NULL);
	atom_store(&self->last_free, ((last_free_t){ 0, 0 }));
	atom_store(&self->reserved, ((reserved_t){ 0, 0, false, false }));
}

bool reserved_swap(reserved_t *self, reserved_t new, bool expect_locked)
{
	assert(self != NULL);
	if (self->lock == expect_locked) {
		*self = new;
		return true;
	}
	return false;
}

bool reserved_set_start(reserved_t *self, size_t row_idx)
{
	// no update if reservation is in progress
	if (!self->lock &&
	    tree_from_row(self->start_row) == tree_from_row(row_idx)) {
		self->start_row = row_idx;
		return true;
	}
	return false;
}

bool reserved_set_lock(reserved_t *self, bool lock)
{
	// only update if reserving is different
	if (self->lock != lock) {
		self->lock = lock;
		return true;
	}
	return false;
}

bool reserved_inc(reserved_t *self, size_t tree_idx, size_t free)
{
	// check if reserved tree is a match for given pfn
	if (!self->present || tree_from_row(self->start_row) != tree_idx)
		return false;

	// check if counter has enough space
	assert(self->free + free <= LLFREE_TREE_SIZE);
	self->free += free;
	return true;
}

bool reserved_dec(reserved_t *self, size_t free)
{
	if (!self->present || self->free < free) {
		// not enough free frames in this tree
		return false;
	}

	self->free -= free;
	return true;
}

bool reserved_dec_or_lock(reserved_t *self, size_t free, bool *locked)
{
	if (!self->present || self->free < free) {
		// not enough free frames in this tree
		if (self->lock)
			return false;

		*locked = true;
		self->lock = true;
		return true;
	}

	self->free -= free;
	return true;
}

bool last_free_inc(last_free_t *self, uint64_t tree_idx)
{
	assert(self != NULL);

	if (self->tree_idx != tree_idx) {
		// if last free was in another tree -> reset
		self->tree_idx = tree_idx;
		self->counter = 1;
	} else if (self->counter < LAST_FREES) {
		// if its the same tree -> increment
		self->counter += 1;
	} else {
		// the heuristic does not have to be updated
		return false;
	}

	return true;
}
