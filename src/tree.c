#include "tree.h"
#include "llfree.h"
#include "llfree_platform.h"
#include "utils.h"

bool tree_put(tree_t *self, treeF_t free)
{
	free += self->free;
	assert(free <= LLFREE_TREE_SIZE);
	self->free = free;

	// promote to Huge if entirely free
	if (free == LLFREE_TREE_SIZE)
		self->kind = LLKIND_HUGE.id;
	return true;
}

bool tree_get(tree_t *self, treeF_t free, llkind_t kind)
{
	if (!self->reserved && self->free >= free &&
	    (self->kind == kind.id || self->free == LLFREE_TREE_SIZE)) {
		self->free -= free;
		self->kind = kind.id;
		return true;
	}
	return false;
}

bool tree_get_demote(tree_t *self, treeF_t free, llkind_t kind)
{
	if (self->free >= free) {
		llkind_t old_kind = self->free == LLFREE_TREE_SIZE ?
					    LLKIND_ZERO :
					    llkind(self->kind);
		// Demote if kind does not match
		self->kind = LL_MIN(old_kind.id, kind.id);
		self->free -= free;
		return true;
	}
	return false;
}

bool tree_reserve(tree_t *self, treeF_t free, treeF_t max, llkind_t kind)
{
	if (!self->reserved &&
	    (self->kind == kind.id || self->free == LLFREE_TREE_SIZE) &&
	    self->free >= free && self->free <= max) {
		*self = tree_new(true, kind, 0);
		return true;
	}
	return false;
}

bool tree_steal_counter(tree_t *self, treeF_t free)
{
	if (self->reserved && self->free >= free) {
		self->free = 0;
		return true;
	}
	return false;
}

bool tree_unreserve(tree_t *self, treeF_t free, llkind_t kind)
{
	assert(self->reserved);
	self->reserved = false;
	self->kind = kind.id;
	return tree_put(self, free);
}

bool tree_put_or_reserve(tree_t *self, treeF_t free, llkind_t kind,
			 bool *reserve, treeF_t min)
{
	ll_unused bool success = tree_put(self, free); // update counter
	assert(success);

	if (unlikely(reserve && *reserve)) {
		// reserve if needed
		*reserve = tree_reserve(self, LL_MAX(min, free),
					LLFREE_TREE_SIZE, kind);
	}
	return true;
}

bool tree_reclaim(tree_t *self, bool *success, bool not_zeroed, bool alloc)
{
	*success = false;
	if (!self->reserved && self->free >= LLFREE_CHILD_SIZE &&
	    (self->kind >= LLKIND_HUGE.id || self->free == LLFREE_TREE_SIZE) &&
	    (!not_zeroed || self->kind != LLKIND_ZERO.id)) {
		if (alloc) {
			if (tree_get(self, LLFREE_CHILD_SIZE,
				     llkind(self->kind))) {
				*success = true;
				return true;
			}
		} else {
			*success = true;
			if (not_zeroed && self->kind == LLKIND_HUGE.id) {
				// Reclaimed pages are zeroed by the caller
				*self = tree_new(self->reserved, LLKIND_ZERO,
						 self->free);
				return true;
			}
			// Success, but no change necessary
		}
	}
	return false;
}

bool tree_undo_reclaim(tree_t *self, bool not_zeroed, bool alloc)
{
	if (alloc) {
		return tree_put(self, LLFREE_CHILD_SIZE);
	}
	if (not_zeroed && self->kind == LLKIND_ZERO.id) {
		*self = tree_new(self->reserved, llkind(self->kind),
				 self->free);
		return true;
	}
	return false;
}

void tree_print(tree_t *self, size_t idx, size_t indent)
{
	if (indent == 0)
		llfree_info_start();
	llfree_info_cont("%stree[%" PRIuS "] { reserved: %d, kind: %" PRIuS
			 ", free: %" PRIuS " }\n",
			 INDENT(indent), idx, self->reserved,
			 (size_t)self->kind, (size_t)self->free);
	if (indent == 0)
		llfree_info_end();
}
