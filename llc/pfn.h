#pragma once

#include <stddef.h>
#include <stdint.h>

#define FRAME_SIZE (1ul << 12) // 4 KiB == 2^12

// pnf is saved here with offset (absolute Type)
typedef uint64_t pfn_at;

// pfn is saved here without offset (relative Type)
typedef uint64_t pfn_rt;

size_t getTreeIdx(pfn_rt pfn);
size_t getChildIdx(pfn_rt pfn);
size_t getAtomicIdx(pfn_rt pfn);
size_t childIDXfromTreeIDX(size_t tree_idx);
pfn_rt pfnFromTreeIdx(size_t tree_idx);
pfn_rt pfnFromChildIdx(size_t idx);
pfn_rt pfnFromAtomicIdx(size_t idx);