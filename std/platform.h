#include <stdio.h>
#include <assert.h>

/// Number of Bytes in cacheline
#define CACHE_SIZE 64u

#define FRAME_BITS 12u
/// Size of a base frame
#define FRAME_SIZE (1u << FRAME_BITS)

/// Order of a huge frame
#define HUGE_ORDER 9u
/// Maximum order that can be allocated
#define MAX_ORDER (HUGE_ORDER + 1u)

/// Num of bits of the larges atomic type of the architecture
#define ATOMIC_ORDER 6u
#define ATOMIC_SIZE (1u << ATOMIC_ORDER)

/// Number of frames in a child
#define CHILD_ORDER HUGE_ORDER
#define CHILD_SIZE (1u << CHILD_ORDER)

/// Number of frames in a tree
#define TREE_CHILDREN_ORDER 5u
#define TREE_CHILDREN (1u << TREE_CHILDREN_ORDER)
#define TREE_ORDER (HUGE_ORDER + TREE_CHILDREN_ORDER)
#define TREE_SIZE (1u << TREE_ORDER)

/// Minimal alignment the llfree requires for its memory range
#define LLFREE_ALIGN (1u << MAX_ORDER << FRAME_BITS)

#define warn(str, ...)                                                \
	printf("\x1b[93m%s:%d: " str "\x1b[0m\n", __FILE__, __LINE__, \
	       ##__VA_ARGS__)

#ifdef VERBOSE
#define info(str, ...)                                                \
	printf("\x1b[90m%s:%d: " str "\x1b[0m\n", __FILE__, __LINE__, \
	       ##__VA_ARGS__)
#else
#define info(str, ...)
#endif

#ifdef DEBUG
#define debug(str, ...)                                               \
	printf("\x1b[90m%s:%d: " str "\x1b[0m\n", __FILE__, __LINE__, \
	       ##__VA_ARGS__)
#else
#define debug(str, ...)
#endif
