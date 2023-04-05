#pragma once

#include <stdint.h>


#define FRAME_SIZE (1ull << 12) // 4 KiB == 2^12

//pageframe number
typedef uint64_t pfn_t;


// uint64_t get_adress(pfn_t pfn){
//     return pfn * FRAME_SIZE;
// }

// pfn_t get_pfn(uint64_t adress){
//     return adress / FRAME_SIZE;
// }