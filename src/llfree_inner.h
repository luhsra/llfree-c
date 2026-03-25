#pragma once

#include "llfree.h"

#include "trees.h"
#include "local.h"
#include "lower.h"

/// The llfree metadata
typedef struct __attribute__((aligned(LLFREE_CACHE_SIZE))) llfree {
	/// Lower allocator
	lower_t lower;
	/// Cpu-local data
	local_t *local;
	/// Manages the tree array
	trees_t trees;

	/// Policy function for tier-based allocation
	llfree_policy_fn policy;
	/// Number of tiers
	uint8_t num_tiers;
} llfree_t;
