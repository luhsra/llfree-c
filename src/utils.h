#pragma once

#include "llfree.h"
#include "llfree_platform.h"

/// Minimal size the LLFree can manage
#define MIN_PAGES (1ul << LLFREE_MAX_ORDER)
/// 64 Bit Addresses - 12 Bit needed for offset inside the Page
#define MAX_PAGES (1ul << (64 - LLFREE_FRAME_BITS))
/// Number of retries
#define RETRIES 8

#define LL_MAX(a, b) ((a) > (b) ? (a) : (b))
#define LL_MIN(a, b) ((a) > (b) ? (b) : (a))
#define LL_MASK(bits) ((1u << (bits)) - 1)

// conversion functions
static inline ll_unused size_t tree_from_frame(uint64_t frame)
{
	return frame >> LLFREE_TREE_ORDER;
}
static inline ll_unused uint64_t frame_from_tree(size_t tree)
{
	return (uint64_t)tree << LLFREE_TREE_ORDER;
}

static inline ll_unused size_t child_from_frame(uint64_t frame)
{
	return frame >> LLFREE_CHILD_ORDER;
}
static inline ll_unused uint64_t frame_from_child(size_t child)
{
	return (uint64_t)child << LLFREE_CHILD_ORDER;
}

static inline ll_unused uint64_t row_from_frame(uint64_t frame)
{
	return frame >> LLFREE_ATOMIC_ORDER;
}
static inline ll_unused uint64_t frame_from_row(uint64_t row)
{
	return row << LLFREE_ATOMIC_ORDER;
}

static inline ll_unused size_t tree_from_row(uint64_t row)
{
	return tree_from_frame(frame_from_row(row));
}
static inline ll_unused uint64_t row_from_tree(size_t tree)
{
	return row_from_frame(frame_from_tree(tree));
}

/// Devides a by b, rounding up the llfree_result
static inline ll_unused size_t div_ceil(uint64_t a, size_t b)
{
	return (a + b - 1) / b;
}
/// Count the number of zero bits beginning at the lowest significant bit
static inline ll_unused size_t trailing_zeros(uint64_t val)
{
	if (val == 0)
		return 8 * sizeof(uint64_t);
	return __builtin_ctzll(val);
}
/// Count the number of zero bits beginning at the most significant bit
static inline ll_unused size_t leading_zeros(uint64_t val)
{
	if (val == 0)
		return 8 * sizeof(uint64_t);
	return __builtin_clzll(val);
}
/// Count the total number of ones
static inline ll_unused size_t count_ones(uint64_t val)
{
	return (size_t)__builtin_popcountll(val);
}
/// Returns the largest multiple of align, less or equal to val
static inline ll_unused size_t align_down(size_t val, size_t align)
{
	assert(({
		size_t zeros = trailing_zeros(align);
		zeros < (8 * sizeof(size_t)) && (1lu << zeros) == align;
	}));
	return val & ~(align - 1);
}
/// Returns the smallest multiple of align, greater or equal to val
static inline ll_unused size_t align_up(size_t val, size_t align)
{
	return align_down(val + align - 1, align);
}
/// Pause CPU for polling
static inline ll_unused void spin_wait(void)
{
#if defined(__x86_64__) || defined(_M_X64) || defined(i386) || \
	defined(__i386__) || defined(__i386) || defined(_M_IX86)
	__asm("pause" ::);
#elif defined(__aarch64__) || defined(_M_ARM64)
	__asm("isb" ::);
#else
#error Unknown architecture
#endif
}
