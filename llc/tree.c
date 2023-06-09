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

  tree_t before = {load(&self->raw)};
  // tree is already reserved
  if (before.flag)
    return ERR_ADDRESS;
  if (cas(self, before, desire) == ERR_OK)
    return before.counter;
  return ERR_RETRY;
}

int unreserve_tree(tree_t *self, uint16_t free_counter) {
  assert(self != NULL);
  tree_t old = {load(&self->raw)};

  assert(free_counter + old.counter <= TREESIZE);
  assert(old.flag == true && "must be reserved to release it");
  tree_t desire = init_tree(free_counter + old.counter, false);

  return cas(self, old, desire);
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
  tree_t old = {load(&self->raw)};

  assert(old.counter + (1 << order) <= TREESIZE);

  tree_t desire = old;
  desire.counter += 1 << order;

  return cas(self, old, desire);
}

int tree_counter_dec(tree_t *self, size_t order) {
  assert(self != NULL);
  tree_t old = {load(&self->raw)};

  assert(old.counter >= 1 << order);
  tree_t desire = old;
  desire.counter -= 1 << order;
  return cas(self, old, desire);
}