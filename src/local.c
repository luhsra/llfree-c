#include "local.h"

void ll_local_init(local_t *self)
{
	assert(self != NULL);
	self->last = (local_history_t){ 0, 0 };
	self->reserved[TREE_MOVABLE] = (reserved_t){ 0, 0, false };
	self->reserved[TREE_FIXED] = (reserved_t){ 0, 0, false };
	self->reserved[TREE_HUGE] = (reserved_t){ 0, 0, false };
}

bool ll_reserved_dec(reserved_t *self, treeF_t free)
{
	if (self->present && self->free >= free) {
		self->free -= free;
		return true;
	}
	return false;
}

bool ll_reserved_dec_check(reserved_t *self, uint64_t tree_idx, treeF_t free)
{
	if (self->present && tree_from_row(self->start_row) == tree_idx &&
	    self->free >= free) {
		self->free -= free;
		return true;
	}
	return false;
}

bool ll_reserved_inc(reserved_t *self, uint64_t tree_idx, treeF_t free)
{
	if (self->present && tree_from_row(self->start_row) == tree_idx) {
		assert(self->free + free <= LLFREE_TREE_SIZE);
		self->free += free;
		return true;
	}
	return false;
}

bool ll_steal(reserved_t *self, treeF_t min)
{
	if (self->present && self->free >= min) {
		*self = (reserved_t){ 0, 0, false };
		return true;
	}
	return false;
}

bool ll_steal_check(reserved_t *self, uint64_t tree_idx, treeF_t min)
{
	if (self->present && tree_from_row(self->start_row) == tree_idx &&
	    self->free >= min) {
		*self = (reserved_t){ 0, 0, false };
		return true;
	}
	return false;
}

bool ll_reserved_swap(reserved_t *self, reserved_t new)
{
	*self = new;
	return true;
}

bool ll_reserved_set_start(reserved_t *self, uint64_t start_row, bool force)
{
	if (force || (self->present && tree_from_row(self->start_row) ==
					       tree_from_row(start_row))) {
		self->start_row = start_row;
		return true;
	}
	return false;
}

static bool frees_inc(local_history_t *self, uint64_t tree_idx)
{
	if (self->idx != tree_idx) {
		// restart for different tree
		self->idx = tree_idx;
		self->frees = 1;
		return true;
	}
	if (self->frees < LAST_FREES) {
		// same tree
		self->frees += 1;
		return true;
	}
	return false;
}

bool ll_local_free_inc(local_t *self, uint64_t tree_idx)
{
	local_history_t frees;
	bool updated = atom_update(&self->last, frees, frees_inc, tree_idx);

	return !updated;
}
