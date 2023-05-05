#include "tree.h"
#include "bitfield.h"
#include <assert.h>
#include <stdint.h>
#include "enum.h"
#include "utils.h"



tree_t init_tree(uint16_t counter, bool flag){
    assert(counter < 0x8000); // max limit for 15 bit
    tree_t ret;
    ret.flag = flag;
    ret.counter = counter;
    return ret;
}



int reserve_tree(tree_t* self){
  assert(self != NULL);
  tree_t old = {load(&self->raw)};

  tree_t desire;
  desire.counter = 0;
  desire.flag = false;

  for(size_t i = 0; i < MAX_ATOMIC_RETRY; ++i){
    //tree is already reserved
    if(old.flag) return ERR_ADDRESS;

    int ret = cas(&self->raw, (uint16_t*) &old.raw, desire.raw);
    if(ret) return old.counter;
  }

  return ERR_RETRY;
}


saturation_level_t tree_status(tree_t* self){
  assert(self != NULL);

  tree_t tree = {load(&self->raw)};
  size_t upper_limit = FIELDSIZE * (1- BREAKPOINT);
  size_t lower_limit = FIELDSIZE * BREAKPOINT;

  if(tree.counter > upper_limit || tree.flag) return ALLOCATED; // reserved trees will be counted as allocated
  if(tree.counter < lower_limit) return FREE;
  return PARTIAL;

}