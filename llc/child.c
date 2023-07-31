#include <assert.h>
#include <stdint.h>

#include "bitfield.h"
#include "child.h"
#include "enum.h"
#include "utils.h"

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

  child_t desire = child_init(CHILDSIZE, false);
  child_t old = {load(&self->raw)};
  // check if child is marked as HP || somehow pages are free
  if (old.flag == false || old.counter != 0)
    return ERR_ADDRESS;
  return cas(self, &old, desire);
}

int reserve_HP(child_t *self) {
  assert(self != NULL);

  child_t desire = child_init(0, true);
  child_t old = {load(&self->raw)};
  // check if already reserved as HP or if not all frames are free
  if (old.flag == true || old.counter != FIELDSIZE)
    return ERR_MEMORY;
  return cas(self, &old, desire);
}