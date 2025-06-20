#pragma once

#include "utils.h"

typedef uint32_t treeF_t;
#define LLFREE_TREE_FREE_BITS \
	((sizeof(treeF_t) * 8) - 1 - 2 - (LLFREE_TREE_CHILDREN_ORDER + 1))
_Static_assert(LLFREE_TREE_FREE_BITS > LLFREE_TREE_ORDER, "Tree free counter");

/// Tree entry
typedef struct tree {
	/// Whether this tree has been reserved.
	bool reserved : 1;
	/// The kind of pages this tree contains.
	uint8_t kind : 2;
	/// Number of free frames in this tree.
	/// If TREE_HUGE, this has to be a multiple of LLFREE_CHILD_SIZE.
	treeF_t free : LLFREE_TREE_FREE_BITS;
	/// The number of zeroed huge pages in this tree.
	/// This requires kind == TREE_HUGE.
	treeF_t zeroed : LLFREE_TREE_CHILDREN_ORDER + 1;
} tree_t;
_Static_assert(sizeof(tree_t) == sizeof(treeF_t), "tree size mismatch");

/// The tree kinds track if a tree contains at least one page of the given kind.
/// Trees can be demoted to a lower kind if memory runs out.
/// They are promoted if all corresponding pages are freed.
typedef struct tree_kind {
	uint8_t id;
} tree_kind_t;
/// Number of tree kinds
#define TREE_KINDS (size_t)(3u)
static inline ll_unused tree_kind_t tree_kind(uint8_t id)
{
	assert(id < TREE_KINDS);
	return (tree_kind_t){ id };
}
/// Contains immovable pages
#define TREE_FIXED tree_kind(0u)
/// Contains movable pages
#define TREE_MOVABLE tree_kind(1u)
/// Contains huge pages (movability is irrelevant)
#define TREE_HUGE tree_kind(2u)

static inline ll_unused char *tree_kind_name(tree_kind_t kind)
{
	assert(kind.id < TREE_KINDS);
	return ((char *[]){ "fixed", "movable", "huge" })[kind.id];
}

/// Tree change transaction
typedef struct tree_change {
	/// The requested tree kind
	tree_kind_t kind;
	union {
		/// Movable/Fixed
		struct {
			/// The number of base frames
			treeF_t frames;
		};
		/// Huge (and potentially Zeroed)
		struct {
			/// The number of huge frames
			uint8_t huge;
			/// The number of zeroed huge frames
			uint8_t zeroed;
		};
	};
} tree_change_t;

static inline tree_change_t tree_change_huge(uint8_t huge, uint8_t zeroed)
{
	assert(huge <= LLFREE_TREE_CHILDREN);
	assert(zeroed <= LLFREE_TREE_CHILDREN);
	return (tree_change_t){ .kind = TREE_HUGE,
				.huge = huge,
				.zeroed = zeroed };
}
static inline tree_change_t tree_change_small(treeF_t frames, bool movable)
{
	assert(frames <= LLFREE_TREE_SIZE);
	return (tree_change_t){ .kind = movable ? TREE_MOVABLE : TREE_FIXED,
				.frames = frames };
}
static inline tree_change_t tree_change(tree_kind_t kind, treeF_t frames,
					uint8_t zeroed)
{
	assert(frames <= LLFREE_TREE_SIZE);
	if (kind.id == TREE_HUGE.id) {
		assert(frames % LLFREE_CHILD_SIZE == 0);
		return tree_change_huge(frames >> LLFREE_CHILD_ORDER, zeroed);
	}
	assert(zeroed == 0);
	return tree_change_small(frames, kind.id == TREE_MOVABLE.id);
}

static inline tree_kind_t tree_kind_flags(llflags_t flags)
{
	if (flags.order >= LLFREE_HUGE_ORDER)
		return TREE_HUGE;
	return flags.movable ? TREE_MOVABLE : TREE_FIXED;
}
static inline tree_change_t tree_change_flags(llflags_t flags)
{
	if (flags.order >= LLFREE_HUGE_ORDER) {
		size_t huge = 1 << (flags.order - LLFREE_CHILD_ORDER);
		assert(huge <= LLFREE_TREE_CHILDREN);
		return tree_change_huge(huge,
					(flags.zeroed && huge == 1) ? huge : 0);
	}
	return tree_change_small(1 << flags.order, flags.movable);
}

typedef struct p_range {
	treeF_t min, max;
} p_range_t;

/// Lower bound used by the tree search heuristics
#define TREE_LOWER_LIM (LLFREE_TREE_SIZE / 16)

/// Create a new tree entry
static inline ll_unused tree_t tree_new(bool reserved, tree_kind_t kind,
					treeF_t free, treeF_t zeroed)
{
	assert(free <= LLFREE_TREE_SIZE);
	assert(zeroed <= LLFREE_TREE_CHILDREN);
	assert(kind.id == TREE_HUGE.id || zeroed == 0);
	assert(kind.id != TREE_HUGE.id || (free % LLFREE_CHILD_SIZE) == 0);
	return (tree_t){ .reserved = reserved,
			 .kind = kind.id,
			 .free = free,
			 .zeroed = zeroed };
}

/// Return frames to a tree.
/// - This might promote the tree to a higher kind.
bool tree_put(tree_t *self, tree_change_t change);

/// Allocate frames from a tree.
/// - If zeroed, only succeeds if the tree has enough zeroed children.
/// - If huge, might fallback to decrement zeroed pages.
/// - Demotes the tree from Huge/Zeroed to Movable/Fixed.
bool tree_get(tree_t *self, tree_change_t change);

/// No fallback from huge to zeroed.
bool tree_get_exact(tree_t *self, tree_change_t change);

/// Reserves a tree if the change would succeed.
bool tree_reserve(tree_t *self, tree_change_t change, treeF_t max);

/// Adds the given counter and clears reserved
bool tree_unreserve(tree_t *self, tree_change_t change);

// Steals the counter of a reserved tree
bool tree_steal_counter(tree_t *self, tree_change_t change);

/// Increment the free counter or reserve if specified
bool tree_put_or_reserve(tree_t *self, tree_change_t change, bool *reserve,
			 treeF_t min);

bool tree_demote(tree_t *self, tree_kind_t kind);

/// Debug print the tree
void tree_print(tree_t *self, size_t idx, size_t indent);
