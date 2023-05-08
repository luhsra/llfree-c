#include "llc.h"
#include "bitfield.h"
#include "child.h"
#include "enum.h"
#include "local.h"
#include "lower.h"
#include "pfn.h"
#include "utils.h"

#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define MAGIC 0xC0CAC01A

/*------------------------------------------------------
  TREE RELATED STUFF T
  ------------------------------------------------------*/
// reads and summs up the childs counter an set the tree counter
// non atomic!
static void init_trees(upper_t *self) {
  assert(self != NULL);

  uint16_t sum = self->lower.childs[0].counter;
  size_t tree_idx = 0;
  for (size_t child_idx = 1; child_idx < self->lower.num_of_childs;
       ++child_idx) {
    if (child_idx % CHILDS_PER_TREE == 0) {
      self->trees[tree_idx] = init_tree(sum, false);
      ++tree_idx;
      sum = 0;
    }
    sum += self->lower.childs[child_idx].counter;
  }
  // init last tree (not fully fsaturated with children)
  if (self->lower.num_of_childs % CHILDS_PER_TREE != 0) {
    self->trees[tree_idx] = init_tree(sum, false);
  }
}

/**
 * @brief searches for a free tree
 * starts searching local for a free or partial tree
 * if not found global
 * @param self pointer to allocator
 * @param core corenumber
 * @param region pfn of the last alloc
 * @return on success treeindex
 *         on faliure ERR_MEMORY
 */
int64_t search_free_tree(upper_t *self, size_t core, pfn_rt region) {
  assert(self != NULL);

  if (region == 0) {
    // no tree was reserved jet -> calculate startig point for region
    region = (self->lower.length / self->cores) * core;
  }

  size_t tree_idx = getTreeIdx(region);
  size_t start =
      tree_idx - tree_idx % 32; // start seaching at beginning of chacheline

  size_t last_partial;
  bool found_partial = false;
  // search in this chacheline starting with given index
  size_t loop = 0;
  for (; loop < 32; ++loop) {
    size_t current_idx = start + loop;

    // last tree is not allways totaly filled with trees
    if (current_idx >= self->num_of_trees)
      break;

    saturation_level_t level = tree_status(&self->trees[current_idx]);
    switch (level) {
    case FREE:
      // Best case: Free tree in the same cacheline
      return current_idx;
      break;
    case PARTIAL:
      if (!found_partial) {
        last_partial = current_idx;
        found_partial = true;
      }
    case ALLOCATED:;
    }
  }
  // no free tree was found but we have a parial filled tree in this cacheline
  if (found_partial)
    return last_partial;

  // no free or partial tree in this chacheline -> search global for a
  // free/partial tree

  start = (start + loop) %
          self->num_of_trees; // start behind the searched chacheline

  for (size_t i = 0; i < self->num_of_trees; i++) {
    size_t current_idx = (start + i) % self->num_of_trees;

    saturation_level_t level = tree_status(&self->trees[current_idx]);
    switch (level) {
    case FREE:
      // found a free Tree someware
      return current_idx;
      break;
    case PARTIAL:
      if (!found_partial) {
        last_partial = current_idx;
        found_partial = true;
      }
    case ALLOCATED:;
    }
  }
  // no free tree existing return a partial tree
  if (found_partial)
    return last_partial;

  assert(
      found_partial &&
      "special case of almost completely allocated memory not implemented jet");
  // no free or partial trees available
  // drain other locale data or give blocked tree???
  return ERR_MEMORY;
}
/*------------------------------------------------------
  END TREE RELATED STUFF
  ------------------------------------------------------*/

static local_t *get_local(upper_t *self, size_t core) {
  size_t idx = core % self->num_of_trees;
  return &self->local[idx];
}

int write_back_tree(upper_t *self, reserved_t old_reserved) {
  assert(self != NULL);

  size_t tree_idx = getTreeIdx(pfnFromAtomicIdx(old_reserved.preferred_index));
  return unreserve_tree(&self->trees[tree_idx], old_reserved.free_counter);
}

/**
 * @brief reserves a new tree for locale or wait until the reservation is done
 * by other cpu
 *
 * @param self pointer to upper
 * @param core this core number
 * @return ERR_OK on success
 *         ERR_MEMORY if no tree was found
 *         ERR_RETRY if tree reservation was not possible
 */
int reserve_new_tree(upper_t *self, size_t core) {
  assert(self != NULL);
  assert(core < self->cores);

  local_t *local = get_local(self, core);

  reserved_t mask = {0};
  mask.reservation_in_progress = true;

  uint64_t before = atomic_fetch_or(&local->reserved.raw, mask.raw);
  if ((before | mask.raw) == before) {
    // already in reservation
    // spinwait for the other CPU to finish
    assert(false && "spinlock not implemented yet");
    return ERR_CORRUPTION;
  }
  // in_reservation is set -> now reserve a new tree
  int counter;
  size_t tree_idx;
  for (size_t i = 0; i < MAX_ATOMIC_RETRY; ++i) {
    tree_idx = search_free_tree(self, core, get_reserved_tree_index(local));
    if (tree_idx < 0) {
      // no free Tree found
      return ERR_MEMORY; // TODO drain?
    }

    counter = reserve_tree(&self->trees[tree_idx]);
    if (counter >= 0) {
      // successfully reserved a tree
      break;
    } 
    // on reservation fail search a new tree and try again
  }
  if(counter < 0){
    // found reserveable tree but was never possible to reserve it
    return ERR_RETRY;
  }

  // successfully marked tree as reserved
  // set new tree
  reserved_t old_reserved;
  int ret =
      set_preferred(local, pfnFromTreeIdx(tree_idx), counter, &old_reserved);
  if (ret == ERR_RETRY) {
    // change locale failed
    // TODO try to infinity? try to write tree counter back?
    assert(false && "set reserved in local data falied");
  }
  // successfully reserved a new tree && in_reservation flag is removed

  // writeback the old counter to trees
  ret = write_back_tree(self, old_reserved);
  if (ret == ERR_RETRY) {
    // writeback counter to tree failed
    // TODO try to infinity? just remove flag and lose some frames=
    assert(false && "set write counter back to trees failed");
  }
  assert(ret == ERR_OK);

  return ERR_OK;
}

/// Creates the allocator and returns a pointer to its data that is passed into
/// all other functions
void *llc_default() {
  upper_t *upper = malloc(sizeof(upper_t));
  assert(upper != NULL);
  upper->meta = malloc(sizeof(struct meta));
  assert(upper->meta != NULL);
  return upper;
}

/// Initializes the allocator for the given memory region, returning 0 on
/// success or a negative error code
int64_t llc_init(void *this, size_t cores, pfn_at start_pfn, size_t len,
                 uint8_t init, uint8_t free_all) {
  (void)(init); // TODO recover
  assert(this != NULL);

  // check if given memory is enough
  if (len < MIN_PAGES || MAX_PAGES > len)
    return ERR_INITIALIZATION;
  // check on unexpeckted Memory allignment
  if (start_pfn % (1 << MAX_ORDER) != 0)
    return ERR_INITIALIZATION;

  upper_t *upper = (upper_t *)this;

  init_default(&upper->lower, start_pfn, len); // allocates memory for lower
  init_lower(&upper->lower, start_pfn, len, free_all);

  upper->meta->magic = MAGIC;
  upper->meta->frames = len;

  upper->num_of_trees = div_ceil(upper->lower.num_of_childs, CHILDS_PER_TREE);
  upper->trees =
      malloc(sizeof(child_t) * upper->num_of_trees); // TODO remove malloc
  assert(upper->trees != NULL);

  upper->cores = cores;
  assert(upper->num_of_trees >= cores); // TODO Wrap cores
  upper->local = malloc(sizeof(local_t) * cores);
  assert(upper->local != NULL);

  // init local data do default 0
  for (size_t local_idx = 0; local_idx < upper->cores; ++local_idx) {
    init_local(&upper->local[local_idx]);
  }

  init_trees(upper);

  upper->meta->crashed = true; // TODO atomic?
  return ERR_OK;
}

/// Allocates a frame and returns its address, or a negative error code
int64_t llc_get(const void *this, size_t core, size_t order) {
  assert(this != NULL);
  (void)(order);

  upper_t *upper = (upper_t *)this;
  local_t *local = get_local(upper, core);
  (void)(local);
  // überprüfe b ein baum reserviert /genügend platz im reservierten baum
  // vorhanden ist
  //-> reserviere neuen Baum

  // reserviere seite im lokalen baum

  return ERR_CORRUPTION;
}

/// Frees a frame, returning 0 on success or a negative error code
int64_t llc_put(const void *this, size_t core, pfn_at frame, size_t order) {
  assert(this != NULL);
  (void)(core);
  (void)(frame);
  (void)(order);
  return 0;
}

/// Returns the total number of frames the allocator can allocate
pfn_at llc_frames(const void *this) {
  assert(this != NULL);
  upper_t *self = (upper_t *)this;

  return self->lower.length;
}

/// Returns number of currently free frames
pfn_at llc_free_frames(const void *this) {
  (void)(this);
  return 0;
}

/// Prints the allocators state for debugging
void llc_debug(const void *this, void (*writer)(void *, char *), void *arg) {
  (void)(this);
  writer(arg, "Hello from LLC!\n");
  writer(arg, "Can be called multiple times...");
}
