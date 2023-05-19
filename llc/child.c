#include <assert.h>
#include <stdint.h>

#include "bitfield.h"
#include "child.h"
#include "enum.h"
#include "utils.h"

child_t init_child(uint16_t counter, bool flag) {
  assert(counter <= CHILDSIZE); // max limit for 9 bit
  child_t ret;
  ret.flag = flag;
  ret.counter = counter;
  return ret;
}

int child_counter_inc(child_t *self) {
  assert(self != NULL);
  child_t desire;
  fetch_update(
      self, desire,
      // If reserved as HP the counter should not be touched
      // cannot increase the counter over the maximum number of free fields
      if (old.flag == true || old.counter >= FIELDSIZE) return ERR_ADDRESS;
      desire = old; ++desire.counter);

  return ERR_OK;
}

int child_counter_dec(child_t *self) {
  assert(self != NULL);
  child_t desire;
  fetch_update(self, desire,
               // If reserved as HP the counter an should not be touched
               if (old.flag || old.counter == 0) return ERR_ADDRESS;
               desire = old; --desire.counter);

  return ERR_OK;
}

int free_HP(child_t *self) {
  assert(self != NULL);

  child_t desire = init_child(CHILDSIZE, false);

  fetch_update(self, desire,
               // check if child is marked as HP || somehow pages are free
               if (old.flag == false || old.counter != 0) return ERR_ADDRESS);

  return ERR_OK;
}

int reserve_HP(child_t *self) {
  assert(self != NULL);

  child_t desire = init_child(0, true);

  fetch_update(
      self, desire,
      // check if already reserved as HP or if not all frames are free
      if (old.flag == true || old.counter != FIELDSIZE) return ERR_MEMORY);

  return ERR_OK;
}

uint16_t get_counter(child_t *self) {
  child_t child = {load(&self->raw)};
  return child.counter;
}
