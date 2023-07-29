#pragma once

#define FRAME_SIZE (1ul << 12) // 4 KiB == 2^12

#define ATOMIC_SHIFT 6
#define CHILD_SHIFT 9
#define TREE_SHIFT 14

#define tree_from_pfn(_N) ({ _N >> TREE_SHIFT; })
#define pfn_from_tree(_N) ({ _N << TREE_SHIFT; })

#define child_from_pfn(_N) ({ _N >> CHILD_SHIFT; })
#define pfn_from_child(_N) ({ _N << CHILD_SHIFT; })

#define atomic_from_pfn(_N) ({ _N >> ATOMIC_SHIFT; })
#define pfn_from_atomic(_N) ({ _N << ATOMIC_SHIFT; })

#define tree_from_atomic(_N) ({ _N >> (TREE_SHIFT - ATOMIC_SHIFT); })