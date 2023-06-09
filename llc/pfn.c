
#include "pfn.h"
#include <assert.h>

size_t getTreeIdx(pfn_rt pfn) {
  assert(pfn < (1ul << 52) && "assumes a Pagesize of 2^12 so the pfn must allways start with 12 zeros");
  return pfn >> 14;
}
size_t getChildIdx(pfn_rt pfn) {
  assert(pfn < (1ul << 52) && "assumes a Pagesize of 2^12 so the pfn must allways start with 12 zeros");
  return pfn >> 9;
}
size_t getAtomicIdx(pfn_rt pfn) {
  assert(pfn < (1ul << 52) && "assumes a Pagesize of 2^12 so the pfn must allways start with 12 zeros");
  return pfn >> 6;
}
pfn_rt pfnFromTreeIdx(size_t tree_idx) { return tree_idx << 14; }
pfn_rt pfnFromChildIdx(size_t tree_idx) { return tree_idx << 9; }
pfn_rt pfnFromAtomicIdx(size_t idx) { return idx << 6; }

size_t childIDXfromTreeIDX(size_t tree_idx){return tree_idx << (14 - 9);}
