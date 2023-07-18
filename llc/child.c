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
  child_t old = {load(&self->raw)};
  // If reserved as HP the counter should not be touched
  // cannot increase the counter over the maximum number of free fields
  if (old.flag == true || old.counter >= FIELDSIZE)
    return ERR_ADDRESS;
  child_t desire = old;
  ++desire.counter;
  return cas(self, &old, desire);
}

int child_counter_dec(child_t *self) {
  assert(self != NULL);
  child_t old = {load(&self->raw)};
  // If reserved as HP the counter an should not be touched
  if (old.flag || old.counter == 0)
    return ERR_MEMORY;

  child_t desire = old;
  --desire.counter;
  return cas(self, &old, desire);
}

int free_HP(child_t *self) {
  assert(self != NULL);

  child_t desire = init_child(CHILDSIZE, false);
  child_t old = {load(&self->raw)};
  // check if child is marked as HP || somehow pages are free
  if (old.flag == false || old.counter != 0)
    return ERR_ADDRESS;
  return cas(self, &old, desire);
}

int reserve_HP(child_t *self) {
  assert(self != NULL);

  child_t desire = init_child(0, true);
  child_t old = {load(&self->raw)};
  // check if already reserved as HP or if not all frames are free
  if (old.flag == true || old.counter != FIELDSIZE)
    return ERR_MEMORY;
  return cas(self, &old, desire);
}

uint16_t get_counter(child_t *self) {
  child_t child = {load(&self->raw)};
  return child.counter;
}

bool is_HP(child_t *self) {
  child_t child = {load(&self->raw)};
  return child.flag;
}
