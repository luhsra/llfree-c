#include "local.h"
#include "assert.h"
#include "bitfield.h"
#include "child.h"
#include "tree.h"
#include "utils.h"
#include <stddef.h>
#include <stdint.h>

void local_init(local_t *self)
{
	assert(self != NULL);
	atom_store(&self->last_free, ((last_free_t){ 0, 0 }));
	atom_store(&self->reserved, ((reserved_t){ 0, 0, false, false }));
}

result_t local_steal(local_t *const self, reserved_t *const old_reservation)
{
	*old_reservation = atom_load(&self->reserved);
	if (!old_reservation->present || old_reservation->free_counter == 0 ||
	    old_reservation->reserving)
		return result(ERR_ADDRESS);
	reserved_t new = { 0 };
	return atom_cmp_exchange(&self->reserved, old_reservation, new) ?
		       result(ERR_OK) :
		       result(ERR_RETRY);
}

bool local_set_new_reserved_tree(reserved_t *reserved, reserve_change_t tree)
{
	assert(reserved != NULL);
	assert(reserved->reserving);
	assert(tree.counter <= TREESIZE);

	const size_t idx = atomic_from_pfn(tree.pfn);

	*reserved = (reserved_t){ .free_counter = tree.counter,
				  .preferred_index = idx,
				  .present = true,
				  .reserving = true };
	return true;
}

bool local_update_last_reserved(reserved_t *self, size_t pfn)
{
	// no update if reservation is in progress
	uint64_t new_reserved = atomic_from_pfn(pfn);
	// TODO: is this check correct? The index will change or not?
	if (self->reserving || tree_from_atomic(self->preferred_index) !=
				       tree_from_atomic(new_reserved)) {
		return false;
	}
	self->preferred_index = new_reserved;
	return true;
}

uint64_t local_get_reserved_pfn(local_t *self)
{
	assert(self != NULL);

	reserved_t pref = atom_load(&self->reserved);
	return pfn_from_atomic(pref.preferred_index);
}

bool local_mark_reserving(reserved_t *self, _void v)
{
	(void)v;
	assert(self != NULL);

	// Already reserving...
	if (self->reserving)
		return false;

	self->reserving = true;
	return true;
}

bool local_unmark_reserving(reserved_t *self, _void v)
{
	(void)v;
	assert(self != NULL);

	if (!self->reserving)
		return false;

	self->reserving = false;
	return true;
}

bool local_inc_counter(reserved_t *self, reserve_change_t change)
{
	assert(self != NULL);
	const size_t atomic_Idx = atomic_from_pfn(change.pfn);

	// check if reserved tree is a match for given pfn
	if (!self->present || tree_from_atomic(self->preferred_index) !=
				      tree_from_atomic(atomic_Idx))
		return ERR_ADDRESS;
	// check if counter has enough space
	assert(self->free_counter <= TREESIZE - change.counter);
	self->free_counter += change.counter;
	return true;
}

bool local_dec_counter(reserved_t *self, size_t order)
{
	assert(self != NULL);

	if (!self->present || self->free_counter < (1 << order)) {
		// not enough free frames in this tree
		return false;
	}

	self->free_counter -= (1 << order);
	return true;
}

bool local_set_free_tree(last_free_t *self, uint64_t tree)
{
	assert(self != NULL);

	// if last free was in another tree -> overwrite last reserved Index
	if (self->last_tree != tree) {
		self->last_tree = tree;
		self->free_counter = 0;
	} else if (self->free_counter < 3) {
		// if the same tree -> increase the counter for this
		self->free_counter += 1;
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
