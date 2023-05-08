#pragma once

#include <stdint.h>
#include <stddef.h>


#define FRAME_SIZE (1ull << 12) // 4 KiB == 2^12

//pnf is saved here with offset (absolute Type)
typedef uint64_t pfn_at;

//pfn is saved here without offset (relative Type)
typedef uint64_t pfn_rt;

size_t getTreeIdx(pfn_rt pfn) { return pfn >> 14; }
size_t getChildIdx(pfn_rt pfn) { return pfn >> 9; }
size_t getAtomicIdx(pfn_rt pfn) { return pfn >> 6; }
pfn_rt pfnFromTreeIdx(size_t tree_idx) { return tree_idx << 14; }
pfn_rt pfnFromAtomicIdx(size_t idx) { return idx << 6; }
