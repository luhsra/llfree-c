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

  tree_t desire = init_tree(0, true);

  for(size_t i = 0; i < MAX_ATOMIC_RETRY; ++i){
    //tree is already reserved
    if(old.flag) return ERR_ADDRESS;

    int ret = cas(&self->raw, (uint16_t*) &old.raw, desire.raw);
    if(ret) return old.counter;
  }

  return ERR_RETRY;
}

int unreserve_tree(tree_t* self, uint16_t free_counter){
  assert(self != NULL);
  tree_t old = {load(&self->raw)};
  for(size_t i = 0; i < MAX_ATOMIC_RETRY; ++i){
    assert(free_counter + old.counter < 0x8000);
    assert(old.flag = true && "must be reserved");
    tree_t new = init_tree(free_counter + old.counter, false);

    int ret = cas(&self->raw, (uint16_t*)&old.raw, new.raw);
    if(ret) return ERR_OK;
  }

  return ERR_RETRY;
}


saturation_level_t tree_status(tree_t* self){
  assert(self != NULL);

  tree_t tree = {load(&self->raw)};
  size_t upper_limit = FIELDSIZE * (1- BREAKPOINT);
  size_t lower_limit = FIELDSIZE * BREAKPOINT;

  if(tree.counter < lower_limit || tree.flag) return ALLOCATED; // reserved trees will be counted as allocated
  if(tree.counter > upper_limit) return FREE;
  return PARTIAL;

}


int tree_counter_inc(tree_t *self, size_t order){
  assert(self != NULL);
  (void)(order);

  tree_t old = {load(&self->raw)};

  for(size_t i = 0; i < MAX_ATOMIC_RETRY; ++i){
    assert(old.counter < 0x8000);
    tree_t new = old;
    new.counter += 1;

    int ret = cas(&self->raw, (uint16_t*) &old.raw, new.raw);
    if(ret){
      return ERR_OK;
    }
  }
  return ERR_RETRY;
}

int tree_counter_dec(tree_t *self, size_t order){
  assert(self != NULL);
  (void)(order);

  tree_t old = {load(&self->raw)};

  for(size_t i = 0; i < MAX_ATOMIC_RETRY; ++i){
    assert(old.counter > 0);
    tree_t new = old;
    new.counter -= 1;

    int ret = cas(&self->raw, (uint16_t*) &old.raw, new.raw);
    if(ret){
      return ERR_OK;
    }
  }
  return ERR_RETRY;

}