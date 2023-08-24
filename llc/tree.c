#include "tree.h"
#include "bitfield.h"
#include "child.h"
#include "enum.h"
#include "lower.h"
#include "utils.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

tree_t tree_init(uint16_t counter, bool flag) {
  assert(counter <= TREESIZE); // max limit for 15 bit
  tree_t ret;
  ret.flag = flag;
  ret.counter = counter;
  return ret;
}

int tree_reserve(tree_t *self) {
  assert(self != NULL);

  tree_t desire = tree_init(0, true);

  tree_t before = {load(&self->raw)};
  // tree is already reserved
  if (before.flag)
    return ERR_ADDRESS;
  if (cas(self, &before, desire) == ERR_OK) {
    assert(before.counter <= TREESIZE);
    return before.counter;
  }
  return ERR_RETRY;
}

int tree_steal_counter(tree_t *self) {
  assert(self != NULL);

  tree_t desire = tree_init(0, true);

  tree_t before = {load(&self->raw)};
  if (cas(self, &before, desire) == ERR_OK)
    return before.counter;
  return ERR_RETRY;
}

int tree_writeback(tree_t *self, uint16_t free_counter) {
  assert(self != NULL);
  tree_t old = {load(&self->raw)};

  assert(free_counter + old.counter <= TREESIZE);

  tree_t desire = tree_init(free_counter + old.counter, false);
  return cas(self, &old, desire);
}

saturation_level_t tree_status(const tree_t *self) {
  assert(self != NULL);

  tree_t tree = {load(&self->raw)};
  const size_t lower_limit = 2 << HP_ORDER;
  const size_t upper_limit = TREESIZE - (8 << HP_ORDER);

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

  return cas(self, &old, desire);
}

int tree_counter_dec(tree_t *self, size_t order) {
  assert(self != NULL);
  tree_t old = {load(&self->raw)};

  assert(old.counter >= 1 << order);
  tree_t desire = old;
  desire.counter -= 1 << order;
  return cas(self, &old, desire);
}
