#include "local.h"

void ll_local_init(local_t *self)
{
	assert(self != NULL);
	atom_store(&self->lock, false);
	self->skip_near_counter = 0;
	self->last_frees = 0;
	self->last_idx = 0;
	self->movable = (reserved_t){ 0, 0, false };
	self->fixed = (reserved_t){ 0, 0, false };
}

void ll_local_lock(local_t *self)
{
	bool expected = false;
	while (!atom_cmp_exchange_weak(&self->lock, &expected, true)) {
		expected = false;
		spin_wait();
	}
}

bool ll_local_try_lock(local_t *self)
{
	bool expected = false;
	if (atom_cmp_exchange(&self->lock, &expected, true)) {
		assert(expected == false);
		return true;
	}
	return false;
}

void ll_local_unlock(local_t *self)
{
	atom_store(&self->lock, false);
}

bool ll_local_free_inc(local_t *self, uint64_t tree_idx)
{
	assert(self != NULL);

	if (self->last_idx != tree_idx) {
		// if last free was in another tree -> reset
		self->last_idx = tree_idx;
		self->last_frees = 1;
		return false;
	}
	if (self->last_frees < LAST_FREES) {
		// if its the same tree -> increment
		self->last_frees += 1;
		return false;
	}
	return true;
}
