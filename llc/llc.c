#include "llc.h"
#include "bitfield.h"
#include "child.h"
#include "enum.h"
#include "local.h"
#include "lower.h"
#include "pfn.h"
#include "utils.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MAGIC 0xC0FFEE

#define get_local(_self, _core)                                                \
  ({ &_self->local[_core % _self->num_of_trees]; })

/**
 * @brief searches a free tree and reserves it if found
 * The cacheline defined by region will be searched first. if no Tree is
 * available all  unreserved trees will be searched.
 * @param self pointer to upper allocator
 * @param core number of core
 * @param has_reserved_tree  if false region will be set to a suitable starting
 * region for the given core.
 * @param region pfn of a tree in witch cacheline is  searched first
 * @return int64_t
 */
static int64_t find_free_Tree_IDX(upper_t const *const self, uint64_t region) {
  assert(self != NULL);

  const size_t tree_idx = tree_from_pfn(region);
  size_t patial_tree_idx;
  bool found_partial_tree = false;

  // search local (same cacheline as region)
  ITERRATE(
      tree_idx, CHILDS_PER_TREE, if (current_i >= self->num_of_trees) continue;

      const saturation_level_t saturation =
          tree_status(&self->trees[current_i]);
      switch (saturation) {
        case ALLOCATED:
          // Do nothing
          break;
        case PARTIAL:
          if (!found_partial_tree) {
            found_partial_tree = true;
            patial_tree_idx = current_i;
          }
          break;
        case FREE:
          return current_i;
      });
  // no free Tree was found within this cacheline -> check if a partial Tree was
  // found
  if (found_partial_tree)
    return patial_tree_idx;

  // no tree reserveable in this chacheline -> search all Trees
  const size_t nextCachlinebegin =
      tree_idx - (tree_idx % CHILDS_PER_TREE) + CHILDS_PER_TREE;
  ITERRATE(
      nextCachlinebegin, self->num_of_trees,
      if (current_i >= self->num_of_trees) continue;
      const saturation_level_t saturation =
          tree_status(&self->trees[current_i]);
      switch (saturation) {
        case ALLOCATED:
          // Do nothing
          break;
        case PARTIAL:
          if (!found_partial_tree) {
            found_partial_tree = true;
            patial_tree_idx = current_i;
          }
          break;
        case FREE:
          return current_i;
      });
  // no free Tree in whole memory available -> return idx of a partial filled
  // Tree
  if (found_partial_tree)
    return patial_tree_idx;

  // All Tress are allocated
  return ERR_MEMORY;
}

/**
 * @brief syncronizes the free_counter of given local with the global counter.
 *
 * @param self pointer to upper allocator
 * @param local pointer to local struct
 * @return True if frames were added to the local tree, false otherwise
 */
static bool sync_with_global(upper_t const *const self, local_t *const local) {
  assert(self != NULL);
  assert(local != NULL);
  // get Index of reserved Tree
  const size_t tree_idx = tree_from_pfn(get_reserved_pfn(local));

  // get countervalue from reserved Tree and set available Frames to 0
  const int64_t counter = update(steal_counter(&self->trees[tree_idx]));

  if (counter == 0)
    // no additional free frames found
    return false;
  reserved_t old = {load(&local->reserved.raw)};
  do {
    // check if treeIdx is still the reserved Tree
    if (tree_from_pfn(pfn_from_atomic(old.preferred_index)) != tree_idx) {
      // the tree we stole from is no longer the reserved tree -> writeback
      // counter to global
      update(writeback_tree(&self->trees[tree_idx], counter));
      return false;
    }
    // add found frames to local counter
    reserved_t desire = old;
    assert(old.free_counter + counter <= TREESIZE);
    desire.free_counter += counter;
    if (cas(&local->reserved, &old, desire) == ERR_OK)
      return true;
  } while (true);
}

/**
 * @brief steals reserved trees from other cores local data
 *
 * @param self upper allocator
 * @param core number of this core
 * @param counter on success will be set to freecount of the new tree else
 * untouched
 * @return on success: Index of reserved tree; ERR_MEMORY if no tree can be
 * stolen.
 */
static int64_t steal_tree(upper_t const *const self, const size_t core) {
  reserved_t old;
  int ret;
  ITERRATE(
      core, self->cores, if (current_i == core) continue;

      ret = try_update(steal(&self->local[current_i], &old));
      if (ret == ERR_OK) {
        // merge freecounter of stolen tree
        const size_t tree_idx =
            tree_from_pfn(pfn_from_atomic(old.preferred_index));
        update(writeback_tree(&self->trees[tree_idx], old.free_counter));

        if (tree_status(&self->trees[tree_idx]) != ALLOCATED) {
          // return idx if some space i left in tree
          return tree_idx;
        }
      };);
  // no success on stealing a reserved Tree -> No memory available
  return ERR_MEMORY;
}

/**
 * @brief Initializes the Treecounters by reading the child counters
 * works non Atomicly!
 * @param self pointer to upper allocator
 */
static void init_trees(upper_t const *const self) {
  assert(self != NULL);

  for (size_t tree_idx = 0; tree_idx < self->num_of_trees; ++tree_idx) {
    uint16_t sum = 0;
    for (size_t child_idx = CHILDS_PER_TREE * tree_idx;
         child_idx < CHILDS_PER_TREE * (tree_idx + 1); ++child_idx) {
      if (child_idx >= self->lower.num_of_childs)
        break;
      sum += self->lower.childs[child_idx].counter;
    }
    self->trees[tree_idx] = init_tree(sum, false);
  }
}

/**
 * @brief Set the preffered tree in local data to given tree and writes back the
 * prevoius tree;
 *
 * @param self pointer to Upper Allocator
 * @param core number of the current core
 * @param pfn pfn to set as preferred Tree
 * @param free_counter number of free Frames in new preferred tree
 * @return ERR_OK
 */
static int set_preferred_and_writeback(upper_t const *const self,
                                       const size_t core, const uint64_t pfn,
                                       const uint16_t free_counter) {

  assert(self != NULL);

  local_t *const local = get_local(self, core);

  reserved_t old_reserved;
  update(set_preferred(local, pfn, free_counter, &old_reserved));
  // successfully reserved a new tree && in_reservation flag is removed

  // writeback the old counter to trees
  if (old_reserved.has_reserved_tree) {
    tree_t *const tree =
        &self->trees[tree_from_atomic(old_reserved.preferred_index)];
    update(writeback_tree(tree, old_reserved.free_counter));
  }
  return ERR_OK;
}

/**
 * @brief reserves a new tree for locale or wait until the reservation is done
 * by other cpu
 *
 * @param self pointer to upper
 * @param core this core number
 * @return ERR_OK on success
 *         ERR_MEMORY if no tree was found
 */
static int reserve_new_tree(upper_t const *const self, size_t const core) {
  assert(self != NULL);

  local_t *const local = get_local(self, core);

  if (mark_as_searchig(local) != ERR_OK) {
    p("reservation already in progress. start spinning\n");
    // already in reservation
    // spinwait for the other CPU to finish
    while ((reserved_t){load(&local->reserved.raw)}.reservation_in_progress) {
    };
    return ERR_OK;
  }

  int64_t tree_idx;
  int counter;
  do {
    uint64_t region = get_reserved_pfn(local);
    if (region == 0)
      region = (self->lower.length / self->cores) * (core % self->cores);
    tree_idx = find_free_Tree_IDX(self, region);
    if (tree_idx < 0) {
      // found no unreserved tree with some space in it -> try steal from other
      // cpus
      tree_idx = steal_tree(self, core);
      if (tree_idx < 0) {
        // not possible to steal a tree -> no memory availabe

        // reset inreservation flag
        unmark_as_searchig(local);
        return ERR_MEMORY;
      }
    }
    // try to reserve freed tree. If no success search another one
    counter = update(reserve_tree(&self->trees[tree_idx]));
  } while (counter < 0);

  // we successfully reserved a tree -> now set it as our local tree.
  set_preferred_and_writeback(self, core, pfn_from_tree(tree_idx), counter);
  unmark_as_searchig(local);
  return ERR_OK;
}

/**
 * @brief Increases the Free counter of given Tree
 * It will increase the local counter if it has the right tree reserved
 * otherwise the global counter is increased
 *
 * @param self pointer to upper allocator
 * @param core number of the core
 * @param pfn in the Tree that will be Incremented
 * @param order size of the frame
 * @return ERR_OK on success
 *         ERR_RETRY if atomic operation failed
 */
static int inc_tree_counter(upper_t const *const self, const size_t core,
                            const uint64_t pfn, size_t order) {
  local_t *local = get_local(self, core);
  int ret = inc_local_free_counter(local, pfn, order);
  if (ret == ERR_ADDRESS) {
    // given tree was not the local tree -> increase global counter
    ret = tree_counter_inc(&self->trees[tree_from_pfn(pfn)], order);
  }
  return ret;
}

/// Creates the allocator and returns a pointer to its data that is passed into
/// all other functions
void *llc_default() {
  upper_t *upper = calloc(1, sizeof(upper_t));
  assert(upper != NULL);
  return upper;
}

/// Initializes the allocator for the given memory region, returning 0 on
/// success or a negative error code
int64_t llc_init(void *this, size_t cores, uint64_t start_frame_adr, size_t len,
                 uint8_t init, uint8_t free_all) {
  assert(this != NULL);
  assert((init == VOLATILE || init == OVERWRITE || init == RECOVER) &&
         "recover not implemented jet");

  // check if given memory is enough
  if (len < MIN_PAGES || len > MAX_PAGES)
    return ERR_INITIALIZATION;
  // check on unexpeckted Memory allignment
  if (start_frame_adr % (1 << MAX_ORDER) != 0)
    return ERR_INITIALIZATION;

  upper_t *self = (upper_t *)this;
  if (init == VOLATILE) {
    self->meta = NULL;
  } else {
    const uint64_t end_managed_memory = start_frame_adr + len * PAGESIZE;
    self->meta = (struct meta *)(end_managed_memory - sizeof(struct meta));
  }

  init_default(&self->lower, start_frame_adr, len, init);
  if (init == RECOVER) {
    if (self->meta->magic != MAGIC)
      return ERR_INITIALIZATION;
    if (self->meta->crashed)
      lower_recover(&self->lower);
  } else {
    init_lower(&self->lower, free_all);
  }

  self->num_of_trees = div_ceil(self->lower.num_of_childs, CHILDS_PER_TREE);
  self->trees = aligned_alloc(
      CACHESIZE, sizeof(child_t) * self->num_of_trees); // TODO remove malloc
  assert(self->trees != NULL);
  if (self->trees == NULL)
    return ERR_INITIALIZATION;

  // check if more cores than trees -> if not shared locale data
  size_t len_locale;
  if (cores > self->num_of_trees) {
    len_locale = self->num_of_trees;
  } else {
    len_locale = cores;
  }
  self->cores = len_locale;
  self->local = malloc(sizeof(local_t) * len_locale); // TODO remove malloc

  assert(self->local != NULL);
  if (self->trees == NULL)
    return ERR_INITIALIZATION;

  // init local data do default 0
  for (size_t local_idx = 0; local_idx < self->cores; ++local_idx) {
    init_local(&self->local[local_idx]);
  }

  init_trees(self);

  if (init != VOLATILE)
    self->meta->crashed = true;
  return ERR_OK;
}

/// Allocates a frame and returns its address, or a negative error code
int64_t llc_get(const void *this, size_t core, size_t order) {
  assert(this != NULL);
  assert(order == 0 || order == HP);
  upper_t const *const self = (upper_t *)this;
  local_t *local = get_local(self, core);

  // if reserved tree is in its initial status
  if (load(&local->reserved.raw) == 0) {
    int ret = update(reserve_new_tree(self, core));
    if (ret == ERR_MEMORY) {
      return ERR_MEMORY;
    }
  }

  int64_t pfn;
  do {
    // decrease local freecounter
    int64_t atomic_idx = update(dec_local_free_counter(local, order));
    if (atomic_idx == ERR_MEMORY) {
      // check if the global counter has some Frames left
      if (sync_with_global(self, local)) {
        atomic_idx = update(dec_local_free_counter(local, order));
      }
    }
    // while local has not enouth frames reserve a new tree
    while (atomic_idx < 0) {
      int ret = reserve_new_tree(self, core);
      if (ret == ERR_MEMORY) {
        // all memory is allocated
        return ERR_MEMORY;
      }
      atomic_idx = update(dec_local_free_counter(local, order));
    }

    // reserved tree with enough space and decremented the local counter
    pfn = lower_get(&self->lower, atomic_idx, order);
    if (pfn >= 0) {
      // successfully reserved a new frame
      break;
    }
    // faliure to reserve a frame in lower allocator -> restore local counter
    int ret = try_update(
        inc_tree_counter(self, core, pfn_from_atomic(atomic_idx), order));
    if (ret != ERR_OK || pfn == ERR_CORRUPTION) {
      // lower is corrupted or the update of local counter caused corruption
      return ERR_CORRUPTION;
    }
  } while (1);

  assert(pfn >= 0);
  // sucessfully reserved frame in lower
  // update reserved idx
  update(update_preferred(local, pfn - self->lower.start_frame_adr));
  return pfn;
}

/// Frees a frame, returning 0 on success or a negative error code
int64_t llc_put(void const *const this, const size_t core,
                const uint64_t frame_adr, const size_t order) {
  assert(this != NULL);
  assert(order == 0 || order == HP);

  upper_t const *const self = (upper_t *)this;

  int ret = lower_put(&self->lower, frame_adr, order);
  if (ret == ERR_ADDRESS) {
    // frame_adr was out of managed memory region or not allocated
    return ERR_ADDRESS;
  }
  assert(ret == ERR_OK);
  // frame is succesfully freed in lower allocator
  local_t *const local = get_local(self, core);
  const uint64_t frame = frame_adr - self->lower.start_frame_adr;
  const size_t tree_idx = tree_from_pfn(frame);

  ret = update(inc_tree_counter(self, core, frame, order));
  assert(ret == ERR_OK);
  // successfully incremented counter in upper allocator

  // set last reserved in local
  ret = update(set_free_tree(local, frame));
  if (ret == ERR_OK) {
    return ERR_OK;
  }

  assert(ret == UPDATE_RESERVED);
  // this tree was the target of multiple consecutive frees
  // -> reserve this tree if it is not completely allocated

  if (tree_status(&self->trees[tree_idx]) == ALLOCATED) {
    return ERR_OK;
  }

  if (mark_as_searchig(local) != ERR_OK) {
    // the local tree is already in reservation process -> ignore the reserve
    // last free tree optimisation
    return ERR_OK;
  }

  int counter = update(reserve_tree(&self->trees[tree_idx]));
  if (counter < 0) {
    // reservation of tree not possible -> ignore the reserve last free tree
    // optimisation
    unmark_as_searchig(local);
    return ERR_OK;
  }
  ret =
      set_preferred_and_writeback(self, core, pfn_from_tree(tree_idx), counter);
  assert(ret == ERR_OK);

  return ERR_OK;
}

/// Returns the total number of frames the allocator can allocate
uint64_t llc_frames(const void *this) {
  assert(this != NULL);
  const upper_t *self = (upper_t *)this;

  return self->lower.length;
}

/// Returns number of currently free frames
uint64_t llc_free_frames(const void *this) {
  assert(this != NULL);
  const upper_t *self = (upper_t *)this;

  return llc_frames(self) - allocated_frames(&self->lower);
}

uint8_t llc_is_free(const void *this, uint64_t frame_adr, size_t order) {
  assert(this != NULL);
  const upper_t *self = (upper_t *)this;
  return lower_is_free(&self->lower, frame_adr, order);
}

/// Prints the allocators state for debugging
void llc_debug(const void *this, void (*writer)(void *, char *), void *arg) {
  (void)(this);
  writer(arg, "Hello from LLC!\n");
  writer(arg, "Can be called multiple times...");
}

void llc_drop(void *this) {
  assert(this != NULL);
  upper_t *self = (upper_t *)this;

  if (self->meta != NULL)
    self->meta->crashed = false;

  if (self->local != NULL) {
    lower_drop(&self->lower);
    free(self->trees);
    free(self->local);
  }
  free(this);
}

/**
 * @brief Debug-function prints information over trees and local Data
 *
 * @param self
 */
void llc_print(const upper_t *self) {
  printf("-----------------------------------------------\nUPPER "
         "ALLOCATOR\nTrees:\t%lu\nCores:\t%lu\n allocated: %lu, free: %lu, "
         "all: %lu\n",
         self->num_of_trees, self->cores,
         llc_frames(self) - llc_free_frames(self), llc_free_frames(self),
         llc_frames(self));

  printf("\nTrees:\n-----------------------------------------------\n");
  if (self->num_of_trees > 20)
    printf("There are over 20 Trees. Print will only contain first and last "
           "10\n\n");

  printf("Nr:\t\t");
  for (size_t i = 0; i < self->num_of_trees; ++i) {
    if (i < 10 || i >= self->num_of_trees - 10)
      printf("%lu\t", i);
  }
  printf("\nreserved:\t");
  for (size_t i = 0; i < self->num_of_trees; ++i) {
    if (i < 10 || i >= self->num_of_trees - 10)
      printf("%d\t", self->trees[i].flag);
  }
  printf("\nfree:\t\t");
  for (size_t i = 0; i < self->num_of_trees; ++i) {
    if (i < 10 || i >= self->num_of_trees - 10)
      printf("%d\t", self->trees[i].counter);
  }
  printf("\n");

  printf("-----------------------------------------------\nLocal "
         "Data:\n-----------------------------------------------\nNr\t\t");
  for (size_t i = 0; i < self->cores; ++i) {
    printf("Core: %lu\t", i);
  }
  printf("\nhas_tree:\t");
  for (size_t i = 0; i < self->cores; ++i) {
    local_t *local = get_local(self, i);
    printf("%d\t", local->reserved.has_reserved_tree);
  }
  printf("\nTreeIDX:\t");
  for (size_t i = 0; i < self->cores; ++i) {
    local_t *local = get_local(self, i);
    printf("%lu\t", tree_from_pfn(get_reserved_pfn(local)));
  }
  printf("\nFreeFrames:\t");
  for (size_t i = 0; i < self->cores; ++i) {
    local_t *local = get_local(self, i);
    printf("%u\t", local->reserved.free_counter);
  }
  printf("\n");

  lower_print(&self->lower);
  fflush(stdout);
}