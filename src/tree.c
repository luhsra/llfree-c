#include "tree.h"
#include "llfree_platform.h"

bool tree_put(tree_t *self, tree_change_t change)
{
	treeF_t free = self->free;
	treeF_t zeroed = self->zeroed;
	tree_kind_t kind = tree_kind(self->kind);

	if ((!self->reserved && free == 0) || change.kind.id < kind.id)
		kind = change.kind;

	if (change.kind.id == TREE_HUGE.id) {
		free += change.huge << LLFREE_CHILD_ORDER;
		zeroed += change.zeroed;
		assert(zeroed <= LLFREE_TREE_CHILDREN);
	} else {
		free += change.frames;
	}
	// promote to Huge if entirely free
	if (free == LLFREE_TREE_SIZE)
		kind = TREE_HUGE;

	*self = tree_new(self->reserved, kind, free, zeroed);
	return true;
}

bool tree_get(tree_t *self, tree_change_t change)
{
	treeF_t free = self->free;
	treeF_t zeroed = self->zeroed;
	tree_kind_t kind = tree_kind(self->kind);

	if (change.kind.id == TREE_HUGE.id) {
		if (free < (change.huge << LLFREE_CHILD_ORDER))
			return false; // not enough free frames
		if (zeroed < change.zeroed)
			return false; // not enough zeroed children

		free -= change.huge << LLFREE_CHILD_ORDER;
		zeroed -= change.zeroed;

		// fallback to zeroed if not enough non-zeroed
		if (zeroed > (free >> LLFREE_CHILD_ORDER))
			zeroed = free >> LLFREE_CHILD_ORDER;
	} else {
		if (free < change.frames)
			return false; // not enough free frames

		free -= change.frames;
		zeroed = 0; // reset zeroed count
		// Demote tree if stricter (number lower)
		// FIXED <- MOVABLE <- HUGE <- ZEROED
		if (kind.id > change.kind.id)
			kind = change.kind;
	}
	*self = tree_new(self->reserved, kind, free, zeroed);
	return true;
}

bool tree_get_exact(tree_t *self, tree_change_t change)
{
	// Do not allow fallback from Huge to Zeroed!
	treeF_t non_zeroed = self->free >> LLFREE_CHILD_ORDER;
	if (change.kind.id == TREE_HUGE.id) {
		assert(non_zeroed >= self->zeroed);
		non_zeroed -= self->zeroed;
		if ((change.huge - change.zeroed) > non_zeroed)
			return false;
	} else {
		if (change.frames >> LLFREE_CHILD_ORDER > non_zeroed)
			return false; // not enough non-zeroed frames
	}

	return tree_get(self, change);
}

bool tree_reserve(tree_t *self, tree_change_t change, treeF_t max)
{
	tree_t copy = *self;
	if (!self->reserved &&
	    (self->kind == change.kind.id || self->free == LLFREE_TREE_SIZE) &&
	    self->free <= max && tree_get_exact(&copy, change)) {
		*self = tree_new(true, change.kind, 0, 0);
		return true;
	}
	return false;
}

bool tree_steal_counter(tree_t *self, tree_change_t change)
{
	tree_t copy = *self;
	if (self->reserved && self->kind == change.kind.id &&
	    tree_get_exact(&copy, change)) {
		*self = tree_new(true, tree_kind(self->kind), 0, 0);
		return true;
	}
	return false;
}

bool tree_unreserve(tree_t *self, tree_change_t change)
{
	assert(self->reserved);
	self->reserved = false;
	return tree_put(self, change);
}

bool tree_put_or_reserve(tree_t *self, tree_change_t change, bool *reserve,
			 treeF_t min)
{
	ll_unused bool success = tree_put(self, change); // update counter
	assert(success);

	if (unlikely(reserve && *reserve)) {
		if (change.kind.id == TREE_HUGE.id)
			change.huge =
				LL_MAX(change.huge, min >> LLFREE_CHILD_ORDER);
		else
			change.frames = LL_MAX(min, change.frames);
		// reserve if needed
		*reserve = tree_reserve(self, change, LLFREE_TREE_SIZE);
	}
	return true;
}

bool tree_demote(tree_t *self, tree_kind_t kind)
{
	if (self->kind > kind.id) {
		self->kind = kind.id;
		self->zeroed = 0; // reset zeroed count
		return true;
	}
	return false;
}

void tree_print(tree_t *self, size_t idx, size_t indent)
{
	if (indent == 0)
		llfree_info_start();
	llfree_info_cont("%stree[%" PRIuS
			 "] { reserved: %d, kind: %s, free: %" PRIuS
			 ", zeroed: %" PRIuS " }\n",
			 INDENT(indent), idx, self->reserved,
			 tree_kind_name(tree_kind(self->kind)),
			 (size_t)self->free, (size_t)self->zeroed);
	if (indent == 0)
		llfree_info_end();
}
