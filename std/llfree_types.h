#pragma once

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdalign.h>

/// Number of Bytes in cacheline
#define LLFREE_CACHE_SIZE 64u

#define LLFREE_FRAME_BITS 12u
/// Size of a base frame
#define LLFREE_FRAME_SIZE (1u << LLFREE_FRAME_BITS)

/// Order of a huge frame
#define LLFREE_HUGE_ORDER 9u
/// Maximum order that can be allocated
#define LLFREE_MAX_ORDER (LLFREE_HUGE_ORDER + 1u)

/// Num of bits of the larges atomic type of the architecture
#define LLFREE_ATOMIC_ORDER 6u
#define LLFREE_ATOMIC_SIZE (1u << LLFREE_ATOMIC_ORDER)

/// Number of frames in a child
#define LLFREE_CHILD_ORDER LLFREE_HUGE_ORDER
#define LLFREE_CHILD_SIZE (1u << LLFREE_CHILD_ORDER)

/// Number of frames in a tree
#define LLFREE_TREE_CHILDREN_ORDER 3u
#define LLFREE_TREE_CHILDREN (1u << LLFREE_TREE_CHILDREN_ORDER)
#define LLFREE_TREE_ORDER (LLFREE_HUGE_ORDER + LLFREE_TREE_CHILDREN_ORDER)
#define LLFREE_TREE_SIZE (1u << LLFREE_TREE_ORDER)

/// Enable reserve on free heuristic
#define LLFREE_ENABLE_FREE_RESERVE false
/// Allocate first from already install huge frames, before falling back to evicted ones
#define LLFREE_PREFER_INSTALLED false

/// Minimal alignment the llfree requires for its memory range
#define LLFREE_ALIGN (1u << LLFREE_MAX_ORDER << LLFREE_FRAME_BITS)

/// Number of bits for the tier field in tree_t
#define LLFREE_TIER_BITS 3u
#define LLFREE_TIER_NONE UINT8_MAX
/// Maximum number of tiers (limited by tree tier field width)
#define LLFREE_MAX_TIERS (1u << LLFREE_TIER_BITS)
