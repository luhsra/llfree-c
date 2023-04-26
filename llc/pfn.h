#pragma once

#include <stdint.h>


#define FRAME_SIZE (1ull << 12) // 4 KiB == 2^12

//pnf is saved here with offset (absolute Type)
typedef uint64_t pfn_at;

//pfn is saved here without offset (relative Type)
typedef uint64_t pfn_rt;


uint64_t get_child_index(pfn_rt);
