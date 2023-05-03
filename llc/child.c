#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>

#include "bitfield.h"
#include "child.h"
#include "enum.h"
#include "utils.h"

child_t init_child(uint16_t counter, bool flag) {
  assert(counter < 0x400); // max limit for 9 bit
  child_t ret;
  ret.flag = flag;
  ret.counter = counter;
  return ret;
}

int child_counter_inc(child_t *self) {
  assert(self != NULL);

  child_t old = {atomic_load(&self->raw)};

  for (size_t i = 0; i < MAX_ATOMIC_RETRY; ++i) {
    // If reserved as HP the counter an should not be touched
    if (old.flag == true)
      return ERR_ADDRESS;
    // cannot increase the counter over the maximum number of free fields
    if (old.counter >= FIELDSIZE)
      return ERR_ADDRESS;

    child_t desire = old;
    ++desire.counter;

    int ret = cas(&self->raw, (uint16_t *)&old.raw, desire.raw);
    if (ret) {
      return ERR_OK;
    }
  }
  return ERR_RETRY;
}

int child_counter_dec(child_t *self) {
  assert(self != NULL);

  child_t old = {load(&self->raw)};

  for (size_t i = 0; i < MAX_ATOMIC_RETRY; ++i) {
    // If reserved as HP the counter an should not be touched
    if (old.flag == true)
      return ERR_ADDRESS;
    // cannot decrease the counter under 0
    if (old.counter == 0)
      return ERR_ADDRESS;

    child_t desire = old;
    --desire.counter;

    int ret = cas(&self->raw, (uint16_t *)&old.raw, desire.raw);
    if (ret) {
      return ERR_OK;
    }
  }
  return ERR_RETRY;
}

int free_HP(child_t *self) {
  assert(self != NULL);

  child_t desire;
  desire.counter = FIELDSIZE;
  desire.flag = false;

  child_t old = {load(&self->raw)};

  for (size_t i = 0; i < MAX_ATOMIC_RETRY; ++i) {

    // child is not marked as HP and the counter should always be zero
    if (old.flag == false || old.counter != 0)
      return ERR_ADDRESS;

    int ret = cas(&self->raw, (uint16_t *)&old.raw, desire.raw);
    if (ret)
      return ERR_OK;
  }

  return ERR_RETRY;
}

int reserve_HP(child_t *self) {
  assert(self != NULL);

  child_t desire;
  desire.counter = 0;
  desire.flag = true;

  child_t old = {load(&self->raw)};

  for (size_t i = 0; i < MAX_ATOMIC_RETRY; ++i) {

    // child was already maked as HP or not all frames are free
    if (old.flag == true || old.counter != FIELDSIZE)
      return ERR_MEMORY;

    int ret = cas(&self->raw, (uint16_t *)&old.raw, desire.raw);
    if (ret)
      return ERR_OK;
  }

  return ERR_RETRY;
}

uint16_t get_counter(child_t *self) { 
  child_t child = {load(&self->raw)};
  return child.counter;
}
