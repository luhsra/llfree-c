#pragma once

#include "bitfield.h"
#include "utils.h"

#define UPDATE_RESERVED -7

/// this struct stores data for the reserved tree
typedef struct reserved {
	uint16_t free : 15; // free frames counter of reserved tree
	uint64_t start_row : 47; // atomic index of reserved tree
	bool present : 1; // true if there is a reserved tree
	bool reserving : 1; // used for spinlock if reservation is in progress
} reserved_t;

// stores information about the last free
typedef struct last_free {
	uint16_t counter : 2; // counter of concurrent free in same tree
	uint64_t last_row : 62; // atomic index of last tree where a frame was freed
} last_free_t;

/**
 * @brief This represents the local CPU data
 * they are allied to the cachesize to avoid false sharing between the CPUs
 */
typedef struct __attribute__((aligned(CACHESIZE))) local {
	_Atomic(reserved_t) reserved;
	_Atomic(last_free_t) last_free;
} local_t;

/// Changes the preferred tree (and free counter) to a new one
bool local_set_reserved(reserved_t *self, size_t pfn, size_t free);

/// Changes the preferred tree (and free counter) to a new one
bool local_swap_reserved(reserved_t *self, reserved_t new, bool expect_reserving);

/// Initialize the per-cpu data
void local_init(local_t *self);

/// set the flag for searching and returns previous status to check if the reservation_in_progress-flag was already set
bool local_mark_reserving(reserved_t *self);
bool local_unmark_reserving(reserved_t *self);

/// Increases the free counter if tree of frame matches the reserved tree
bool local_inc_counter(reserved_t *self, size_t tree_idx, size_t free);

/// Decrements the free counter
bool local_dec_counter(reserved_t *self, size_t free);

// resets the reserved tree and returns the old reservation
result_t local_steal(local_t *const self, reserved_t *const old_reservation);

// sets last_free to tree of given frame
// returns ERR_OK on success
// ERR_Retry on atomic operation fail
// UPDATE_RESERVED after 4 consecutive free on the same tree
bool local_inc_last_free(last_free_t *self, uint64_t tree);

/// updates the atomic index of last reserved tree
// returns ERR_OK on success
// ERR_RETRY on atomic operation fail
bool local_reserve_index(reserved_t *self, size_t pfn);

/// spin-wait until in-reservation flag becomes false
void local_wait_reserving(const local_t *const self);
