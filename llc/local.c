#include "local.h"
#include "assert.h"
#include "bitfield.h"
#include "child.h"
#include "enum.h"
#include "pfn.h"
#include "tree.h"
#include "utils.h"
#include <stddef.h>
#include <stdint.h>

void init_local(local_t *self) {
  assert(self != NULL);
  self->last_free.raw = 0;
  self->reserved.raw = 0;
}

int steal(local_t* const self, reserved_t* const old_reservation){
  *old_reservation = (reserved_t){load(&self->reserved.raw)};
  if(!old_reservation->has_reserved_tree || old_reservation->reservation_in_progress) return ERR_ADDRESS;
  reserved_t new = {0};

  return cas(&self->reserved, old_reservation, new);
}

int set_preferred(local_t *self, pfn_rt pfn, uint16_t free_count,
                  reserved_t *old_reservation) {
  assert(self != NULL);
  assert(free_count <= TREESIZE);

  size_t idx = getAtomicIdx(pfn);

  reserved_t desire = {0};
  desire.free_counter = free_count;
  desire.preferred_index = idx;
  desire.has_reserved_tree = true;
  desire.reservation_in_progress = false;

  old_reservation->raw = load(&self->reserved.raw);
  assert(old_reservation->reservation_in_progress);
  return cas(&self->reserved, old_reservation, desire);
}

int update_preferred(local_t* const self, pfn_rt pfn){
  reserved_t prev = {load(&self->reserved.raw)};
  reserved_t desire = prev;
  desire.preferred_index = getAtomicIdx(pfn);
  return cas(&self->reserved, &prev, desire);
}

pfn_rt get_reserved_pfn(local_t *self) {
  assert(self != NULL);

  reserved_t pref = {load(&self->reserved.raw)};
  return pfnFromAtomicIdx(pref.preferred_index);
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

int unmark_as_searchig(local_t *self) {
  assert(self != NULL);

  reserved_t mask = {0ul};
  mask.reservation_in_progress = true;
  mask.raw = ~mask.raw;
  uint64_t before = atomic_fetch_and(&self->reserved.raw, mask.raw);
  if ((before & mask.raw) == before)
    return ERR_RETRY; // no change means flag was already reset
  return ERR_OK;
}


int inc_local_free_counter(local_t *const self, const pfn_rt frame, const size_t order) {
  assert(self != NULL);
  const size_t atomic_Idx = getAtomicIdx(frame);

  reserved_t old = {load(&self->reserved.raw)};
  // check if reserved tree is a match for given pfn
  if (old.preferred_index != atomic_Idx)
    return ERR_ADDRESS;
  // check if counter has enough space
  assert(old.free_counter <= TREESIZE - (1 << order));
  reserved_t desire = old;
  desire.free_counter += 1 << order;
  return cas(&self->reserved, &old, desire);
}

int dec_local_free_counter(local_t *const self,const  size_t order) {
  assert(self != NULL);

  reserved_t old = {load(&self->reserved.raw)};
  if (!old.has_reserved_tree || old.free_counter < (1 << order)) {
    // not enough free frames in this tree
    return ERR_MEMORY;
  }
  reserved_t desire = old;
  desire.free_counter -= (1 << order);
  if(cas(&self->reserved, &old, desire) == ERR_OK){
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

  } else {
    // in this tree were 4 consecutive frees
    // -> no change and message to reserve this tree
    return UPDATE_RESERVED;
  }

  return cas(&self->last_free, &old, desire);
}