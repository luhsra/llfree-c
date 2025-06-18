#include "tree.h"
#include "linux/stddef.h"

bool tree_reserve(tree_t *self, treeF_t min, treeF_t max, uint8_t kind)
{
	assert(min < max);

	if (!self->reserved && min <= self->free && self->free <= max &&
	    (self->kind == kind ||
	     (self->free == LLFREE_TREE_SIZE && kind <= self->kind &&
	      self->kind != TREE_ZEROED))) {
		*self = tree_new(0, true, kind, 0);
		return true;
	}
	return false;
}

bool tree_steal_counter(tree_t *self, treeF_t min)
{
	if (self->reserved && self->free >= min) {
		*self = tree_new(0, true, self->kind, 0);
		return true;
	}
	return false;
}

bool tree_unreserve(tree_t *self, treeF_t free, uint8_t kind, treeF_t zeroed)
{
	assert(self->reserved);
	treeF_t f = self->free + free;
	assert(f <= LLFREE_TREE_SIZE);
	treeF_t z = kind >= TREE_HUGE ? self->zeroed + zeroed : 0;
	assert(z <= LLFREE_TREE_CHILDREN);

	if (f == LLFREE_TREE_SIZE)
		kind = TREE_HUGE; // promote to Huge or Zeroed
	if (z > 0 && kind == TREE_HUGE)
		// promote to Zeroed if zeroed children are present
		kind = TREE_ZEROED;
	if (z == 0 && kind == TREE_ZEROED)
		// demote to Huge if no zeroed children
		kind = TREE_HUGE;

	*self = tree_new(f, false, kind, z);
	return true;
}

bool tree_inc(tree_t *self, treeF_t free, bool zeroed)
{
	assert(self->free + free <= LLFREE_TREE_SIZE);

	self->free += free;
	// promote to Huge if entirely free
	if (self->free == LLFREE_TREE_SIZE) {
		self->kind = self->zeroed ? TREE_ZEROED : TREE_HUGE;
	}
	if (zeroed && self->kind >= TREE_HUGE) {
		self->kind = TREE_ZEROED; // promote to Zeroed
		treeF_t z = self->zeroed + (free >> LLFREE_CHILD_ORDER);
		assert(z <= LLFREE_TREE_CHILDREN);
		self->zeroed = z;
	}
	return true;
}

bool tree_dec(tree_t *self, treeF_t free, bool zeroed)
{
	if (!self->reserved && self->free >= free) {
		if (zeroed) {
			if (self->zeroed < (free >> LLFREE_CHILD_ORDER))
				return false; // not enough zeroed children

			self->zeroed -= free >> LLFREE_CHILD_ORDER;
			// demote to Huge if no zeroed children
			if (self->zeroed == 0)
				self->kind = TREE_HUGE;
		}
		self->free -= free;
		// If zeroed is requested but not available,
		// allocate a dirty huge page and dont set the zeroed flag
		return true;
	}
	return false;
}

bool tree_dec_force(tree_t *self, treeF_t free, uint8_t kind)
{
	// Overwrite tree kind if priority higher (number lower)
	// FIXED > MOVABLE > HUGE > ZEROED
	if (!self->reserved && self->free >= free) {
		if (kind == self->kind)
			return tree_dec(self, free, false); // no change in kind

		// TODO: This is valid, but we might loose some zeroed pages
		// Still the lower allocator might or might not allocate a
		// zeroed page if we want a dirty one.
		// It is currently not enforced...
		if (kind < self->kind) {
			self->kind = kind;
			self->zeroed = 0; // reset zeroed count
		}
		self->free -= free;
		return true;
	}
	return false;
}

bool tree_dec_zeroing(tree_t *self, treeF_t free, bool require_zeroed)
{
	if (self->reserved || self->free < free)
		return false;

	if (require_zeroed) {
		// check if there are enough zeroed children
		if (self->zeroed < (free >> LLFREE_CHILD_ORDER))
			return false; // not enough zeroed children
		self->zeroed -= free >> LLFREE_CHILD_ORDER;
		if (self->zeroed == 0)
			// demote to Huge if no zeroed children
			self->kind = TREE_HUGE;
	} else {
		// check if there are enough non-zeroed children
		treeF_t non_zeroed =
			(self->free >> LLFREE_CHILD_ORDER) - self->zeroed;
		if (free < non_zeroed)
			return false; // not enough non-zeroed children
	}
	self->free -= free;

	return true;
}

bool tree_inc_or_reserve(tree_t *self, treeF_t free, bool *reserve, treeF_t min)
{
	// TODO: Do we have to handle zeroed pages here?
	ll_unused bool success = tree_inc(self, free, 0); // update counter
	assert(success);

	if (reserve && *reserve) // reserve if needed
		*reserve =
			tree_reserve(self, min, LLFREE_TREE_SIZE, self->kind);
	return true;
}
