#pragma once

#include "bitfield.h"
#include "child.h"
#include "pfn.h"
#include <stdalign.h>
#include <stdint.h>

#define UPDATE_RESERVED 1
// metadata for llc
struct meta {
  uint32_t magic;
  bool crashed;
};

// TODO Description
typedef struct reserved {
  union {
    _Atomic(uint64_t) raw;
    struct {
      uint16_t free_counter : 15;
      uint64_t preferred_index : 46;
      bool has_reserved_tree : 1;
      bool reservation_in_progress : 1;
      uint8_t unused : 1;
    };
  };
} reserved_t;

// TODO Description
typedef struct last_free {
  union {
    _Atomic(uint64_t) raw;
    struct {
      uint16_t free_counter : 2;
      uint64_t last_free_idx : 46;
      uint16_t unused : 16;
    };
  };
} last_free_t;

/**
 * @brief This represents the local CPU data
 * they are alliged to the chachesize to avoid false sharing betrwwn the CPUs
 */
typedef struct __attribute__((aligned(CACHESIZE))) local {
  reserved_t reserved;
  last_free_t last_free;

} local_t;

/**
 * @brief atomicly sets the preferred tree to given tree
 * @param self pointer to local object
 * @param pfn pfn of new tree
 * @param free_count count of free Frames
 * @param old_reservation the old reservation
 * @return ERR_RETRY if atomic operation fails
 *         ERR_OK on success
 */
int local_set_new_preferred_tree(local_t *self, uint64_t pfn, uint16_t free_count,
                  reserved_t *old_reservation);

// init and set preferred to magic value
void local_init(local_t *self);

#define local_has_reserved_tree(_local) \
({(reserved_t){load(&_local->reserved.raw)}.has_reserved_tree;})

// get the reserved tree index
uint64_t local_get_reserved_pfn(local_t *self);
// set the flag for searching
int local_mark_as_searchig(local_t *self);
// reset the flag for searching
int local_unmark_as_searchig(local_t *self);

// set last free index
int local_inc_counter(local_t *const self, const uint64_t frame,
                           const size_t order);
int local_dec_counter(local_t *const self, const size_t order);

// resetzs the reserved tree and returns the old
int local_steal(local_t *const self, reserved_t *const old_reservation);

// sets last_free to tree of given frame
// returns UPDATE_RESERVED after 4 consecutive free on the same tree
int local_set_free_tree(local_t *self, uint64_t frame);

int local_update_last_reserved(local_t *const self, uint64_t pfn);