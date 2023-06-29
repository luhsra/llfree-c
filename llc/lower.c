#include "lower.h"
#include "bitfield.h"
#include "child.h"
#include "enum.h"
#include "pfn.h"
#include "utils.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <stdio.h>

void init_default(lower_t * const self, pfn_at start_pfn, size_t len) {
  self->start_pfn = start_pfn;
  self->length = len;

  self->num_of_childs = div_ceil(self->length, FIELDSIZE);
  self->fields = malloc(sizeof(bitfield_512_t) * self->num_of_childs);
  assert(self->fields != NULL);
  self->childs = malloc(sizeof(child_t) * self->num_of_childs);
  assert(self->childs != NULL);
}

int init_lower(lower_t const * const self, bool free_all) {
  assert(self != NULL);

  for (size_t i = 0; i < self->num_of_childs - 1; i++) {
    self->fields[i] = init_field(0, free_all);
    self->childs[i] = init_child(free_all ? FIELDSIZE : 0, false);
  }
  size_t frames_in_last_field = self->length % FIELDSIZE;
  self->fields[self->num_of_childs - 1] =
      init_field(frames_in_last_field, free_all);

  if (frames_in_last_field == 0)
    frames_in_last_field = FIELDSIZE;
  self->childs[self->num_of_childs - 1] =
      init_child(free_all ? frames_in_last_field : 0, false);
  return ERR_OK;
}

int64_t get_HP(lower_t const*const self, pfn_rt atomic_idx) {
  assert(self != 0);

  size_t idx = getChildIdx(pfnFromAtomicIdx(atomic_idx));
  size_t offset = idx % CHILDS_PER_TREE;
  size_t start_idx = idx - offset;

  for (size_t i = 0; i < CHILDS_PER_TREE; ++i) {
    size_t current_idx = start_idx + offset;
    if (update(reserve_HP(&self->childs[current_idx]) == ERR_OK)) {
      return pfnFromChildIdx(current_idx) + self->start_pfn;
    }
    ++offset;
    offset %= CHILDS_PER_TREE;
  }
  return ERR_MEMORY;
}

int64_t lower_get(lower_t const*const self, int64_t atomic_idx, size_t order) {
  assert(order == 0 || order == HP);
  pfn_rt pfn = pfnFromAtomicIdx(atomic_idx);
  if (pfn >= self->length)
    return ERR_ADDRESS;

  if (order == HP)
    return get_HP(self, atomic_idx);

  const size_t start_idx = getChildIdx(pfn);
  const size_t base = start_idx - (start_idx % CHILDS_PER_TREE);

  for (size_t i = 0; i < CHILDS_PER_TREE; ++i) {
    size_t current_idx = base + ((start_idx + i) % CHILDS_PER_TREE);

    if (current_idx >= self->num_of_childs)
      current_idx = base;
      
    if (update(child_counter_dec(&self->childs[current_idx])) == ERR_OK) {
      int64_t pos;
      do {
        pos = set_Bit(&self->fields[current_idx]);
        if (pos >= 0) {
          // found and reserved a frame
          return pfnFromChildIdx(current_idx) + pos + self->start_pfn;
        }
      } while (pos == ERR_RETRY);

      assert(pos == ERR_MEMORY);
      // not possible to reserve a frame even in child were enough free frames

      int ret = try_update(child_counter_inc(&self->childs[current_idx]));
      if(ret != ERR_OK){
        // not possible to restore childcounter to correct value
        return ERR_CORRUPTION;
      }

      //childcounter is restored
      assert(false); //TODO make sure caller handels this case correctly
      return ERR_RETRY;
    }
  }

  return ERR_MEMORY;
}

int lower_put(lower_t const*const self, pfn_at frame_adr, size_t order) {
  assert(order == 0 || order == HP);

  // chek if outside of managed space
  if (frame_adr >= self->start_pfn + self->length ||
      frame_adr < self->start_pfn)
    return ERR_ADDRESS;
  pfn_rt frame = frame_adr - self->start_pfn;
  size_t child_index = getChildIdx(frame);

  if (order == 9) {
    return update(free_HP(&self->childs[child_index]));
  }

  size_t field_index = (frame) % FIELDSIZE;
  int ret = reset_Bit(&self->fields[child_index], field_index);
  if (ret != ERR_OK)
    return ERR_ADDRESS;

  ret = update(child_counter_inc(&self->childs[child_index]));
  if (ret == ERR_ADDRESS) {
    // somehow we are not able to increase the child_counter ->try reset the
    // bitfield
  }

  return ERR_OK;
}



int is_free(lower_t const*const self, pfn_rt frame, size_t order) {
  assert(order == 0 || order == HP);

  // check if outside of managed space
  if (frame >= self->start_pfn + self->length || frame < self->start_pfn)
    return ERR_ADDRESS;
  
  size_t child_index = getChildIdx(frame);

  child_t child = {load(&self->childs[child_index].raw)};

  if(order == HP){
    return (!child.flag && child.counter == FIELDSIZE);
  }

  size_t field_index = frame % FIELDSIZE;

  if (child.counter < 1 << order)
    return false;

  return is_free_bit(&self->fields[child_index], field_index);
}

size_t allocated_frames(lower_t const*const self) {
  size_t counter = self->length;
  for (size_t i = 0; i < self->num_of_childs; i++) {
    counter -= get_counter(&self->childs[i]);
  }
  return counter;
};

void print_lower(lower_t const*const self) {
  printf("\n-------------------------------------\nlower allocator: with %zu childs\n%lu/%lu frames are allocated\nChilds:\n",
         self->num_of_childs, allocated_frames(self), self->length);
  if (self->num_of_childs > 20)
    printf(
        "There are over 20 Childs. Print will only contain first and last 10\n\n");
  printf("Nr:\t\t");
  for (size_t i = 0; i < self->num_of_childs; ++i) {
    if (i < 10 || i >= self->num_of_childs - 10) printf("%lu\t", i);
  }
  printf("\nHp?:\t\t");
  for (size_t i = 0; i < self->num_of_childs; ++i) {
    if (i < 10 || i >= self->num_of_childs - 10) printf("%d\t", self->childs[i].flag);
  }
  printf("\free:\t\t");
  for (size_t i = 0; i < self->num_of_childs; ++i) {
    if (i < 10 || i >= self->num_of_childs - 10) printf("%d\t", self->childs[i].counter);
  }
  printf("\n");
}

void lower_drop(lower_t const* const self) {
  assert(self != NULL);

  free(self->childs);
  free(self->fields);
}
