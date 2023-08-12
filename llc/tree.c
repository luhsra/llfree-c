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

int tree_writeback_and_reserve(tree_t *self, uint16_t free_counter) {
  assert(self != NULL);
  tree_t old = {load(&self->raw)};

  assert(free_counter + old.counter <= TREESIZE);
  tree_t desire = tree_init(0, true);

  int ret = cas(self, &old, desire);
  if (ret == ERR_OK)
    return free_counter + old.counter;
  else
    return ret;
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
  const size_t lower_limit = 1 << HP_ORDER;
  const size_t upper_limit = TREESIZE - lower_limit;

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

int64_t tree_find_reserveable(tree_t const *const trees, const uint64_t len,
                              const uint64_t pfn_region, const uint64_t order,
                              const uint64_t vercinity, const uint64_t core) {
  const uint64_t tree_idx = tree_from_pfn(pfn_region);
  assert(tree_idx < len);
  (void)core;
  (void)vercinity;
  (void)order;



  const uint64_t start_idx = tree_idx - (tree_idx % CHILDS_PER_TREE);
  uint64_t free_idx = len;

  //search inside of currend cacheline
  for (size_t i = 1; i <= (vercinity < CHILDS_PER_TREE ? vercinity : CHILDS_PER_TREE); ++i) {
    uint64_t idx = start_idx + (i & 1 ? i / 2 : CHILDS_PER_TREE - i / 2);
    if (idx >= len) continue;

    const saturation_level_t sat = tree_status(&trees[idx]);
    switch (sat) {
    case ALLOCATED:
      continue;
      break;
    case FREE:
      free_idx = idx;
      break;
    case PARTIAL:
      return idx;
      break;
    }
  }
  // if found, return idx of a free tree
  if (free_idx != len)
    return free_idx;


  // search outside of currend cacheline for a partial tree
  for(size_t i = 1; i <= len - CHILDS_PER_TREE; ++i){
    uint64_t idx = (start_idx + (i & 1 ? CHILDS_PER_TREE + i / 2 : -i / 2)) % len;

    saturation_level_t sat = tree_status(&trees[idx]);
    switch (sat) {
      case PARTIAL:
        return idx;
        break;
      default:
        continue;
    }
  }

  // search whole tree for a tree with enough free frames
  const uint16_t min_val = 1 << order;
  ITERATE_TOGGLE(tree_idx, len,
          const tree_t tree = {load(&trees[current_i].raw)};
          if (!tree.flag && tree.counter >= min_val) return current_i;
  );
  return ERR_MEMORY;
}