#include "pfn.h"

size_t getTreeIdx(pfn_rt pfn) { return pfn >> 14; }
size_t getChildIdx(pfn_rt pfn) { return pfn >> 9; }
size_t getAtomicIdx(pfn_rt pfn) { return pfn >> 6; }
pfn_rt pfnFromTreeIdx(size_t tree_idx) { return tree_idx << 14; }
pfn_rt pfnFromAtomicIdx(size_t idx) { return idx << 6; }
