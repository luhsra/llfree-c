#include "local.h"
#include "assert.h"
#include "bitfield.h"
#include "child.h"
#include "enum.h"
#include "tree.h"
#include "utils.h"
#include <stddef.h>
#include <stdint.h>

void local_init(local_t *self) {
  assert(self != NULL);
  self->last_free.raw = 0;
  self->reserved.raw = 0;
}

int local_steal(local_t *const self, reserved_t *const old_reservation) {
  *old_reservation = (reserved_t){load(&self->reserved.raw)};
  if (!old_reservation->has_reserved_tree ||
      old_reservation->free_counter == 0 ||
      old_reservation->reservation_in_progress)
    return ERR_ADDRESS;
  reserved_t new = {0};
  return cas(&self->reserved, old_reservation, new);
}

int local_set_new_preferred_tree(local_t *self, uint64_t pfn,
                                 uint16_t free_count,
                                 reserved_t *old_reservation) {
  assert(self != NULL);
  assert(free_count <= TREESIZE);

  const size_t idx = atomic_from_pfn(pfn);

  reserved_t desire = {0};
  desire.free_counter = free_count;
  desire.preferred_index = idx;
  desire.has_reserved_tree = true;
  desire.reservation_in_progress = true;

  old_reservation->raw = load(&self->reserved.raw);
  assert(old_reservation->reservation_in_progress);
  return cas(&self->reserved, old_reservation, desire);
}

int local_update_last_reserved(local_t *const self, uint64_t pfn) {
  reserved_t prev = {load(&self->reserved.raw)};
  // no update if reservation is in progress
  uint64_t new_reserved = atomic_from_pfn(pfn);
  if (prev.reservation_in_progress || tree_from_atomic(prev.preferred_index) !=
                                          tree_from_atomic(new_reserved)) {
    return ERR_OK;
  }
  reserved_t desire = prev;
  desire.preferred_index = new_reserved;
  return cas(&self->reserved, &prev, desire);
}

uint64_t local_get_reserved_pfn(local_t *self) {
  assert(self != NULL);

  reserved_t pref = {load(&self->reserved.raw)};
  return pfn_from_atomic(pref.preferred_index);
}

reserved_t local_mark_as_searching(local_t *self) {
  assert(self != NULL);

  reserved_t mask = {0ul};
  mask.reservation_in_progress = true;

  return (reserved_t){atomic_fetch_or(&self->reserved.raw, mask.raw)};
}

int local_unmark_as_searching(local_t *self) {
  assert(self != NULL);

  reserved_t mask = {0ul};
  mask.reservation_in_progress = true;
  mask.raw = ~mask.raw;
  uint64_t before = atomic_fetch_and(&self->reserved.raw, mask.raw);
  if ((before & mask.raw) == before)
    return ERR_RETRY; // no change means flag was already reset
  return ERR_OK;
}

int local_inc_counter(local_t *const self, const uint64_t frame,
                      const size_t order) {
  assert(self != NULL);
  const size_t atomic_Idx = atomic_from_pfn(frame);

  reserved_t old = {load(&self->reserved.raw)};
  // check if reserved tree is a match for given pfn
  if (!old.has_reserved_tree || tree_from_atomic(old.preferred_index) != tree_from_atomic(atomic_Idx))
    return ERR_ADDRESS;
  // check if counter has enough space
  assert(old.free_counter <= TREESIZE - (1 << order));
  reserved_t desire = old;
  desire.free_counter += 1 << order;
  return cas(&self->reserved, &old, desire);
}

int64_t local_dec_counter(local_t *const self, const size_t order) {
  assert(self != NULL);

  reserved_t old = {load(&self->reserved.raw)};
  if (!old.has_reserved_tree || old.free_counter < (1 << order)) {
    // not enough free frames in this tree
    return ERR_MEMORY;
  }
  reserved_t desire = old;
  desire.free_counter -= (1 << order);
  if (cas(&self->reserved, &old, desire) == ERR_OK) {
    return old.preferred_index;
  }
  return ERR_RETRY;
}

int local_set_free_tree(local_t *self, uint64_t frame) {
  assert(self != NULL);

  last_free_t old = {load(&self->last_free.raw)};
  last_free_t desire;
  // if last free was in another tree -> overwrite last reserved Index
  if (tree_from_atomic(old.last_free_idx) != tree_from_pfn(frame)) {
    desire.last_free_idx = atomic_from_pfn(frame);;
    desire.free_counter = 0;

    // if the same tree -> increase the counter for this
  } else if (old.free_counter < 3) {
    desire = old;
    desire.free_counter += 1;

  } else {
    // in this tree were 4 consecutive frees
    // -> no change and message to reserve this tree
    return UPDATE_RESERVED;
  }

  return cas(&self->last_free, &old, desire);
}


void local_wait_for_completion(const local_t* const self){
  reserved_t res;
  do{
    res = (reserved_t){load(&self->reserved.raw)};
  }while(res.reservation_in_progress);
}