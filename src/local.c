#include "local.h"
#include "assert.h"
#include "tree.h"
#include "utils.h"
#include <stddef.h>

void local_init(local_t *self)
{
	assert(self != NULL);
	atom_store(&self->last_free, ((last_free_t){ 0, 0 }));
	atom_store(&self->reserved, ((reserved_t){ 0, 0, false, false }));
}

result_t local_steal(local_t *const self, reserved_t *const old_reservation)
{
	*old_reservation = atom_load(&self->reserved);
	if (!old_reservation->present || old_reservation->free == 0 ||
	    old_reservation->reserving)
		return result(ERR_ADDRESS);
	reserved_t new = { 0 };
	return atom_cmp_exchange(&self->reserved, old_reservation, new) ?
		       result(ERR_OK) :
		       result(ERR_RETRY);
}

bool local_set_reserved(reserved_t *reserved, size_t pfn, size_t free)
{
	assert(reserved != NULL);
	assert(free <= TREESIZE);

	const size_t idx = row_from_pfn(pfn);

	*reserved = (reserved_t){ .free = free,
				  .start_idx = idx,
				  .present = true,
				  .reserving = true };
	return true;
}

bool local_reserve_index(reserved_t *self, size_t pfn)
{
	// no update if reservation is in progress
	if (self->reserving) {
		return false;
	}
	self->start_idx = row_from_pfn(pfn);
	return true;
}

bool local_mark_reserving(reserved_t *self)
{
	// no update if reservation is in progress
	if (self->reserving)
		return false;

	self->reserving = true;
	return true;
}

bool local_unmark_reserving(reserved_t *self)
{
	// no update if reservation is in progress
	if (!self->reserving)
		return false;

	self->reserving = false;
	return true;
}

bool local_inc_counter(reserved_t *self, size_t pfn, size_t order)
{
	const size_t tree_idx = tree_from_pfn(pfn);

	// check if reserved tree is a match for given pfn
	if (!self->present || tree_from_row(self->start_idx) != tree_idx)
		return false;

	// check if counter has enough space
	assert(self->free + (1 << order) <= TREESIZE);
	self->free += (1 << order);
	return true;
}

bool local_dec_counter(reserved_t *self, size_t order)
{
	if (!self->present || self->free < (1 << order)) {
		// not enough free frames in this tree
		return false;
	}

	self->free -= (1 << order);
	return true;
}

bool local_inc_last_free(last_free_t *self, uint64_t tree)
{
	assert(self != NULL);

	// if last free was in another tree -> overwrite last reserved Index
	if (self->last_row != tree) {
		self->last_row = tree;
		self->counter = 0;
	} else if (self->counter < 3) {
		// if the same tree -> increase the counter for this
		self->counter += 1;
	}

	return true;
}

void local_wait_for_completion(const local_t *const self)
{
	while (({
		reserved_t r = atom_load(&self->reserved);
		r.reserving;
	})) {
		spin_wait();
	}
}
