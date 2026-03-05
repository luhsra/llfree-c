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

	/// Policy function for tier-based allocation
	llfree_policy_fn policy;
	/// Default tier for allocations
	uint8_t default_tier;
	/// Number of tiers
	uint8_t num_tiers;
} llfree_t;
