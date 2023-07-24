#include "lower.h"
#include "bitfield.h"
#include "child.h"
#include "enum.h"
#include "local.h"
#include "pfn.h"
#include "utils.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <stdio.h>

void init_default(lower_t *const self, pfn_at start_pfn, size_t len,
                  uint8_t init) {
  self->start_pfn = start_pfn;
  self->length = len;

  self->num_of_childs = div_ceil(self->length, FIELDSIZE);

  if (init == VOLATILE) {
    self->fields =
        aligned_alloc(CACHESIZE, sizeof(bitfield_512_t) * self->num_of_childs);
    assert(self->fields != NULL);
    self->childs =
        aligned_alloc(CACHESIZE, sizeof(child_t) * self->num_of_childs);
    assert(self->childs != NULL);

  } else {
    /*
      Laycout in persistent Memory:
      |-------------------------------------------------------------------------------------------|
      |           Managed Frames                 | childs |      bitfields
      |(void)| metadata |
      |-------------------------------------------------------------------------------------------|

      void is unused memory of a size smaler than a Frame.
      its clippings, because we can not hand out half a frame.

     */
    uint64_t bytes_for_Bitfields = self->num_of_childs * CACHESIZE;

    uint64_t bytes_for_childs = self->num_of_childs * 2;
    bytes_for_childs +=
        CACHESIZE -
        (bytes_for_childs % CACHESIZE); // round up to get complete cachelines

    uint64_t bytes_for_meta = sizeof(struct meta);
    bytes_for_meta +=
        CACHESIZE -
        (bytes_for_meta % CACHESIZE); // round up to get complete cachelines

    uint64_t memory_for_controlstructures =
        bytes_for_Bitfields + bytes_for_childs + bytes_for_meta;

    uint64_t pages_needed = div_ceil(memory_for_controlstructures, PAGESIZE);

    self->length -= pages_needed;

    char *start_data = (char *)(self->start_pfn + self->length * PAGESIZE);

    self->childs = (child_t *)start_data;
    self->fields = (bitfield_512_t *)(start_data + bytes_for_childs);
  }
}

int init_lower(lower_t const *const self, bool all_free) {
  assert(self != NULL);

  for (size_t i = 0; i < self->num_of_childs - 1; i++) {
    self->fields[i] = init_field(0, all_free);
    self->childs[i] =
        init_child(all_free ? FIELDSIZE : 0, all_free ? false : true);
  }
  size_t frames_in_last_field = self->length % FIELDSIZE;
  self->fields[self->num_of_childs - 1] =
      init_field(frames_in_last_field, all_free);

  if (frames_in_last_field == 0)
    frames_in_last_field = FIELDSIZE;
  self->childs[self->num_of_childs - 1] =
      init_child(all_free ? frames_in_last_field : 0, !all_free && frames_in_last_field == FIELDSIZE);
  return ERR_OK;
}

int lower_recover(lower_t *self) {
  for (size_t i = 0; i < self->num_of_childs; ++i) {
    if (self->childs[i].flag) {
      // if this was reserved as Huge Page set counter and all frames to 0
      self->childs[i].counter = 0;
      self->fields[i] = init_field(0, true);
    } else {
      // not a Huge Page -> count free Frames and set as counter
      self->childs[i].counter = FIELDSIZE - count_Set_Bits(&self->fields[i]);
    }
  }

  return ERR_OK;
}

int64_t get_HP(lower_t const *const self, pfn_rt atomic_idx) {
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

/**
 * @brief searches and reserves a frame in Bitfield with with index
 * 
 * @param self pointer to lower
 * @param idx index of the Bitfield that is searched
 * @return adress of the reseved frame on success
 *         ERR_MEMORY if no free Frame were found
 */
static int64_t reserve_in_Bitfield(const lower_t *self, const size_t idx) {
  const int64_t pos = update(set_Bit(&self->fields[idx]));
  if (pos >= 0) {
    // found and reserved a frame
    return pfnFromChildIdx(idx) + pos + self->start_pfn;
  }
  return ERR_MEMORY;
}

int64_t lower_get(lower_t const *const self, int64_t atomic_idx, size_t order) {
  assert(order == 0 || order == HP);
  pfn_rt pfn = pfnFromAtomicIdx(atomic_idx);
  if (pfn >= self->length)
    return ERR_ADDRESS;

  if (order == HP)
    return get_HP(self, atomic_idx);

  const size_t start_idx = getChildIdx(pfn);

  ITERRATE(
      start_idx, CHILDS_PER_TREE,
      if (current_i >= self->num_of_childs) continue;

      if (update(child_counter_dec(&self->childs[current_i])) == ERR_OK) {
        int64_t pfn_adr = reserve_in_Bitfield(self, current_i);

        assert(pfn_adr >= 0 && "because of the counter in child there must always be a frame left");
        if(pfn_adr == ERR_MEMORY) return ERR_CORRUPTION;

        return pfn_adr;
      });

  return ERR_MEMORY;
}

void convert_HP_to_regular(child_t *child, bitfield_512_t *field) {
  const uint64_t before = atomic_fetch_or(&field->rows[0], 0xFFFFFFFFFFFFFFFF);
  if (before != 0) {
    // another thread ist trying to breakeup this HP
    // -> wait for their completion
    while (is_HP(child)) {
    }
    return;
  }
  for (size_t i = 1; i < N; ++i) {
    atomic_fetch_or(&field->rows[i], 0xFFFFFFFFFFFFFFFF);
  }

  child_t mask = init_child(0, true);
  child_t before_c = {atomic_fetch_and(&child->raw, ~mask.raw)};
  assert(before_c.flag == true);
  return;
}

int lower_put(lower_t const *const self, pfn_at frame_adr, size_t order) {
  assert(order == 0 || order == HP);

  // chek if outside of managed space
  if (frame_adr >= self->start_pfn + self->length ||
      frame_adr < self->start_pfn)
    return ERR_ADDRESS;
  pfn_rt frame_number = frame_adr - self->start_pfn;
  const size_t child_index = getChildIdx(frame_number);
  child_t *child = &self->childs[child_index];
  bitfield_512_t *field = &self->fields[child_index];

  if (order == HP) {
    return update(free_HP(child));
  }

  if (is_HP(child)) {
    convert_HP_to_regular(child, field);
  }

  size_t field_index = (frame_number) % FIELDSIZE;
  int ret = reset_Bit(field, field_index);
  if (ret != ERR_OK)
    return ERR_ADDRESS;

  ret = update(child_counter_inc(child));
  if (ret == ERR_ADDRESS) {
    assert(false && "should never be possible");
  }

  return ERR_OK;
}

int is_free(lower_t const *const self, pfn_rt frame, size_t order) {
  assert(order == 0 || order == HP);

  // check if outside of managed space
  if (frame >= self->start_pfn + self->length || frame < self->start_pfn)
    return ERR_ADDRESS;

  size_t child_index = getChildIdx(frame);

  child_t child = {load(&self->childs[child_index].raw)};

  if (order == HP) {
    return (!child.flag && child.counter == FIELDSIZE);
  }

  size_t field_index = frame % FIELDSIZE;

  if (child.counter < 1 << order)
    return false;

  return is_free_bit(&self->fields[child_index], field_index);
}

size_t allocated_frames(lower_t const *const self) {
  size_t counter = self->length;
  for (size_t i = 0; i < self->num_of_childs; i++) {
    counter -= get_counter(&self->childs[i]);
  }
  return counter;
};

void print_lower(lower_t const *const self) {
  printf("\n-------------------------------------\nlower allocator: with %zu "
         "childs\n%lu/%lu frames are allocated\nChilds:\n",
         self->num_of_childs, allocated_frames(self), self->length);
  if (self->num_of_childs > 20)
    printf("There are over 20 Childs. Print will only contain first and last "
           "10\n\n");
  printf("Nr:\t\t");
  for (size_t i = 0; i < self->num_of_childs; ++i) {
    if (i < 10 || i >= self->num_of_childs - 10)
      printf("%lu\t", i);
  }
  printf("\nHp?:\t\t");
  for (size_t i = 0; i < self->num_of_childs; ++i) {
    if (i < 10 || i >= self->num_of_childs - 10)
      printf("%d\t", self->childs[i].flag);
  }
  printf("\nfree:\t\t");
  for (size_t i = 0; i < self->num_of_childs; ++i) {
    if (i < 10 || i >= self->num_of_childs - 10)
      printf("%d\t", self->childs[i].counter);
  }
  printf("\n");
}

void lower_drop(lower_t const *const self) {
  assert(self != NULL);

  free(self->childs);
  free(self->fields);
}
