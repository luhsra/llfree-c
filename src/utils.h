#pragma once

#include "llfree.h"
#include "llfree_platform.h"

/// Minimal size the LLFree can manage
#define MIN_PAGES (1ul << LLFREE_MAX_ORDER)
/// 64 Bit Addresses - 12 Bit needed for offset inside the Page
#define MAX_PAGES (1ul << (64 - LLFREE_FRAME_BITS))
/// Number of retries
#define RETRIES 4

#define LL_MAX(a, b) ((a) > (b) ? (a) : (b))
#define LL_MIN(a, b) ((a) > (b) ? (b) : (a))
#define LL_MASK(bits) ((1u << (bits)) - 1)

/// Optional size_t type
ll_def_optional(size_t, ll, 0);

/// Iterates over a Range between multiples of len starting at idx.
///
/// Starting at idx up to the next Multiple of len (exclusive). Then the next
/// step will be the highest multiple of len less than idx. (_base_idx)
/// Loop will end after len iterations.
/// code will be executed in each loop.
/// The current loop value can accessed by current_i
#define for_offsetted(start, len, current_i)                              \
	for (size_t _i = 0, _offset = (start) % (len),                    \
		    _base_idx = (start) - _offset, (current_i) = (start); \
	     _i < (len);                                                  \
	     _i = _i + 1, (current_i) = _base_idx + ((_i + _offset) % (len)))

#define ll_opt_map(src, dst_ty, func) \
	((src).present ? dst_ty##_some(func((src).value)) : dst_ty##_none())

/// Unique identifier for a huge page
typedef struct huge_id {
	size_t value;
} huge_id_t;
static inline ll_unused huge_id_t huge_id(size_t value)
{
	return (huge_id_t){ .value = value };
}
ll_def_optional(struct huge_id, huge_id, { 0 });

/// Unique identifier for a row in the bitfield
typedef struct row_id {
	uint64_t value;
} row_id_t;
static inline ll_unused row_id_t row_id(uint64_t value)
{
	return (row_id_t){ .value = value };
}
ll_def_optional(struct row_id, row_id, { 0 });

static inline ll_unused tree_id_t tree_from_frame(frame_id_t frame)
{
	return tree_id(frame.value >> LLFREE_TREE_ORDER);
}
static inline ll_unused frame_id_t frame_from_tree(tree_id_t tree)
{
	return frame_id((uint64_t)tree.value << LLFREE_TREE_ORDER);
}

static inline ll_unused huge_id_t child_from_frame(frame_id_t frame)
{
	return huge_id((size_t)(frame.value >> LLFREE_CHILD_ORDER));
}
static inline ll_unused frame_id_t frame_from_child(huge_id_t child)
{
	return frame_id((uint64_t)child.value << LLFREE_CHILD_ORDER);
}
static inline ll_unused huge_id_t huge_from_frame(frame_id_t frame)
{
	return huge_id((size_t)(frame.value >> LLFREE_HUGE_ORDER));
}
static inline ll_unused frame_id_t frame_from_huge(huge_id_t huge)
{
	return frame_id((uint64_t)huge.value << LLFREE_HUGE_ORDER);
}

static inline ll_unused row_id_t row_from_frame(frame_id_t frame)
{
	return row_id(frame.value >> LLFREE_ATOMIC_ORDER);
}
static inline ll_unused frame_id_t frame_from_row(row_id_t row)
{
	return frame_id(row.value << LLFREE_ATOMIC_ORDER);
}

static inline ll_unused tree_id_t tree_from_row(row_id_t row)
{
	return tree_from_frame(frame_from_row(row));
}
static inline ll_unused row_id_t row_from_tree(tree_id_t tree)
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

static inline ll_unused size_t log2(uint64_t val)
{
	assert(val > 0);
	return (size_t)(63 - leading_zeros(val));
}

static inline ll_unused size_t next_pow2(size_t val)
{
	if (val <= 1)
		return 1;
	return (size_t)1 << (log2(val - 1) + 1);
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

static inline ll_unused const char *INDENT(size_t indent)
{
	size_t const max_indent = 32;
	const char *spaces = "                                ";
	if (indent >= max_indent / 4)
		return spaces;
	return &spaces[max_indent - (indent * 4)];
}
