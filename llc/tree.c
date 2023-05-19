#include "tree.h"
#include "bitfield.h"
#include "enum.h"
#include "utils.h"
#include <assert.h>
#include <stdint.h>

tree_t init_tree(uint16_t counter, bool flag) {
  assert(counter <= TREESIZE); // max limit for 15 bit
  tree_t ret;
  ret.flag = flag;
  ret.counter = counter;
  return ret;
}

int reserve_tree(tree_t *self) {
  assert(self != NULL);

  tree_t desire = init_tree(0, true);

  tree_t before = fetch_update(self, desire,
                               // tree is already reserved
                               if (old.flag) return ERR_ADDRESS;);
  return before.counter;
}

int unreserve_tree(tree_t *self, uint16_t free_counter) {
  assert(self != NULL);
  tree_t desire;
  fetch_update(self, desire, assert(free_counter + old.counter <= TREESIZE);
               assert(old.flag == true && "must be reserved to release it");
               desire = init_tree(free_counter + old.counter, false);

  );
  return ERR_OK;
}

saturation_level_t tree_status(const tree_t *self) {
  assert(self != NULL);

  tree_t tree = {load(&self->raw)};
  size_t upper_limit = TREESIZE * (1 - BREAKPOINT);
  size_t lower_limit = TREESIZE * BREAKPOINT;

  if (tree.counter < lower_limit || tree.flag)
    return ALLOCATED; // reserved trees will be counted as allocated
  if (tree.counter > upper_limit)
    return FREE;
  return PARTIAL;
}

int tree_counter_inc(tree_t *self, size_t order) {
  assert(self != NULL);
  tree_t desire;
  fetch_update(self, desire, assert(old.counter + (1 << order) <= TREESIZE);
               desire = old; desire.counter += 1 << order;);

  return ERR_OK;
}

int tree_counter_dec(tree_t *self, size_t order) {
  assert(self != NULL);
  tree_t desire;
  fetch_update(self, desire, assert(old.counter >= 1 << order); desire = old;
               desire.counter -= 1 << order;);
  return ERR_OK;
}