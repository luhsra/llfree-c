#pragma once

#include "llfree.h"

#include "tree.h"
#include "local.h"
#include "lower.h"

/// The llfree metadata
typedef struct __attribute__((aligned(LLFREE_CACHE_SIZE))) llfree {
	/// Lower allocator
	lower_t lower;
	/// Cpu-local data
	local_t *local;
	/// Array of tree entries
	_Atomic(tree_t) *trees;
	size_t trees_len;

	/// false means there are no zeroed pages or we should search for non-zeroed pages
	_Atomic(bool) contains_zeroed;
	/// false means there are no huge pages or we should look for zero pages
	_Atomic(bool) contains_huge;
} llfree_t;
