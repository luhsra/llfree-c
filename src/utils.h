#pragma once

#include "llfree.h"
#include "platform.h"

// conversion functions
#define tree_from_pfn(_N) ((_N) >> LLFREE_TREE_ORDER)
#define pfn_from_tree(_N) ((_N) << LLFREE_TREE_ORDER)

#define child_from_pfn(_N) ((_N) >> LLFREE_CHILD_ORDER)
#define pfn_from_child(_N) ((_N) << LLFREE_CHILD_ORDER)

#define row_from_pfn(_N) ((_N) >> LLFREE_ATOMIC_ORDER)
#define pfn_from_row(_N) ((_N) << LLFREE_ATOMIC_ORDER)

#define tree_from_row(_N) tree_from_pfn(pfn_from_row(_N))
#define row_from_tree(_N) row_from_pfn(pfn_from_tree(_N))

/// Minimal size the LLFree can manage
#define MIN_PAGES (1ul << LLFREE_MAX_ORDER)
/// 64 Bit Addresses - 12 Bit needed for offset inside the Page
#define MAX_PAGES (1ul << (64 - LLFREE_FRAME_BITS))
/// Number of retries
#define RETRIES 4

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))

/// Devides a by b, rounding up the llfree_result
static inline _unused size_t div_ceil(uint64_t a, size_t b)
{
	return (a + b - 1) / b;
}
/// Count the number of zero bits beginning at the lowest significant bit
static inline _unused int trailing_zeros(uint64_t val)
{
	if (val == 0)
		return -1;
	return __builtin_ctzll(val);
}
/// Count the number of zero bits beginning at the most significant bit
static inline _unused int leading_zeros(uint64_t val)
{
	if (val == 0)
		return -1;
	return __builtin_clzll(val);
}
/// Count the total number of ones
static inline _unused size_t count_ones(uint64_t val)
{
	return __builtin_popcountll(val);
}
/// Returns the largest multiple of align, less or equal to val
static inline _unused size_t align_down(size_t val, size_t align)
{
	assert(align > 0 && (1 << trailing_zeros(align) == align));
	return val & ~(align - 1);
}
/// Returns the smallest multiple of align, greater or equal to val
static inline _unused size_t align_up(size_t val, size_t align)
{
	return align_down(val + align - 1, align);
}
/// Pause CPU for polling
static inline _unused void spin_wait(void)
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
