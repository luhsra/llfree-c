#pragma once

#include "bitfield.h"
#include "utils.h"

#define UPDATE_RESERVED -7

/// this struct stores data for the reserved tree
typedef struct reserved {
	uint16_t free_counter : 15; // free frames counter of reserved tree
	uint64_t start_idx : 47; // atomic index of reserved tree
	bool present : 1; // true if there is a reserved tree
	bool reserving : 1; // used for spinlock if reservation is in progress
} reserved_t;

// stores information about the last free
typedef struct last_free {
	uint16_t free_counter : 2; // counter of concurrent free in same tree
	uint64_t last_tree : 62; // atomic index of last tree where a frame was freed
} last_free_t;

/**
 * @brief This represents the local CPU data
 * they are allied to the cachesize to avoid false sharing between the CPUs
 */
typedef struct __attribute__((aligned(CACHESIZE))) local {
	_Atomic(reserved_t) reserved;
	_Atomic(last_free_t) last_free;
} local_t;

typedef struct reserve_change {
	size_t pfn;
	size_t counter;
} reserve_change_t;

/**
 * @brief atomically sets the preferred tree to given tree
 * @param self pointer to local object
 * @param pfn pfn of new tree
 * @param free_count count of free Frames
 * @param old_reservation the old reservation
 * @return ERR_RETRY if atomic operation fails
 *         ERR_OK on success
 */
bool local_set_reserved(reserved_t *self, reserve_change_t tree);

// init with no tree reserved
void local_init(local_t *self);

// set the flag for searching and returns previous status to check if the reservation_in_progress-flag was already set
bool local_mark_reserving(reserved_t *self, _void v);
// reset the flag for searching
bool local_unmark_reserving(reserved_t *self, _void v);

/**
 * @brief increases the free counter if tree of frame matches the reserved tree
 *
 * @param self pointer to local data
 * @param frame to determine the tree
 * @param order order of returned frame to calculate the amount of returned regular frames
 * @return int ERR_OK in success
 *         ERR_ADDRESS if trees not match
 *         ERR_RETRY on atomic operation fail
 */
bool local_inc_counter(reserved_t *self, reserve_change_t);

/**
 * @brief decrements the free counter by 2^order
 * returns ERR_OK on success
 * ERR_RETRY on atomic operation fail
 * ERR_MEMORY if counter value was not high enough
 */
bool local_dec_counter(reserved_t *self, size_t order);

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
void local_wait_for_completion(const local_t *const self);
