#include "local.h"
#include "assert.h"
#include "child.h"
#include "enum.h"
#include "pfn.h"
#include "tree.h"
#include "utils.h"
#include <stddef.h>
#include <stdint.h>

void init_local(local_t *self) {
  assert(self != NULL);
  self->last_free.free_counter = 0;
  self->last_free.last_free_idx = 0;
  self->reserved.free_counter = 0;
  self->reserved.preferred_index = 0;
  self->reserved.reservation_in_progress = false;
  self->reserved.has_reserved_tree = false;
}

int set_preferred(local_t *self, pfn_rt pfn, uint16_t free_count,
                  reserved_t *old_reservation) {
  assert(self != NULL);
  assert(free_count < 0x8000);

  size_t idx = getAtomicIdx(pfn);

  reserved_t desire = {0};
  desire.free_counter = free_count;
  desire.preferred_index = idx;
  desire.has_reserved_tree = true;

  old_reservation->raw = load(&self->reserved.raw);
  assert(old_reservation->reservation_in_progress);
  return cas(&self->reserved, *old_reservation, desire);
}

pfn_rt get_reserved_tree_index(local_t *self) {
  assert(self != NULL);

  reserved_t pref = {load(&self->reserved.raw)};
  return pfnFromAtomicIdx(pref.preferred_index);
}

pfn_rt get_last_free_tree_index(local_t *self) {
  assert(self != NULL);

  last_free_t pref = {load(&self->last_free.raw)};
  return pfnFromAtomicIdx(pref.last_free_idx << 6);
}

bool has_reserved_tree(local_t *self) {
  assert(self != NULL);
  reserved_t pref = {load(&self->reserved.raw)};

  return pref.reservation_in_progress;
}

int mark_as_searchig(local_t *self) {
  assert(self != NULL);

  reserved_t mask = {0ul};
  mask.reservation_in_progress = true;

  uint64_t before = atomic_fetch_or(&self->reserved.raw, mask.raw);
  if ((before | mask.raw) == before)
    return ERR_RETRY; // no change means flag was already set
  return ERR_OK;
}

bool is_searching(local_t *self) {
  assert(self != NULL);
  reserved_t pref = {load(&self->reserved.raw)};

  return pref.reservation_in_progress;
}

int inc_free_counter(local_t *self, pfn_rt frame, size_t order) {
  assert(self != NULL);
  size_t atomic_Idx = getAtomicIdx(frame);

  reserved_t old = {load(&self->reserved.raw)};
  // check if reserved tree is a match for given pfn
  if (old.preferred_index != atomic_Idx)
    return ERR_ADDRESS;
  // check if counter has enough space
  assert(old.free_counter <= TREESIZE - (1 << order));
  reserved_t desire = old;
  desire.free_counter += 1 << order;
  return cas(&self->reserved, old, desire);
}

int dec_free_counter(local_t *self, size_t order) {
  assert(self != NULL);

  reserved_t old = {load(&self->reserved.raw)};
  if (!old.has_reserved_tree || old.free_counter < (1 << order)) {
    // not enough free frames in this tree
    return ERR_MEMORY;
  }
  reserved_t desire = old;
  desire.free_counter -= (1 << order);
  if(cas(&self->reserved, old, desire) == ERR_OK){
    return old.preferred_index;
  }
  return ERR_RETRY;
}

int set_free_tree(local_t *self, pfn_rt frame) {
  assert(self != NULL);
  size_t atomic_Idx = getAtomicIdx(frame);

  last_free_t old = {load(&self->last_free.raw)};
  last_free_t desire;
  // if last free was in another tree -> overwrite last reserved Index
  if (old.last_free_idx != atomic_Idx) {
    desire.last_free_idx = atomic_Idx;
    desire.free_counter = 0;

    // if the same tree -> increase the counter for this
  } else if (old.free_counter < 3) {
    desire = old;
    desire.free_counter += 1;

    // in this tree were 4 consecutive frees
    // -> no change and message to reserve this tree
  } else {
    return UPDATE_RESERVED;
  }

  return cas(&self->last_free, old, desire);
}