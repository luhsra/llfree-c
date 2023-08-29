#pragma once

#include "bitfield.h"
#include "child.h"
#include <stdalign.h>
#include <stdint.h>

#define UPDATE_RESERVED -7

/// this struct stores data for the reserved tree
typedef struct reserved {
	union {
		_Atomic(uint64_t) raw; //used for atomic access
		struct {
			uint16_t free_counter : 15; // free frames counter of reserved tree
			uint64_t preferred_index : 46; // atomic index of reserved tree
			bool has_reserved_tree : 1; // true if there is a reserved tree
			bool reservation_in_progress : 1; // used for spinlock if reservation is in progress
			uint8_t unused : 1;
		};
	};
} reserved_t;

// stores information about the last free
typedef struct last_free {
	union {
		_Atomic(uint64_t) raw; // used for atomic access
		struct {
			uint16_t free_counter : 2; // counter of concurrent free in same tree
			uint64_t last_free_idx : 46; // atomic index of last tree where a frame was freed
			uint16_t unused : 16;
		};
	};
} last_free_t;

/**
 * @brief This represents the local CPU data
 * they are allied to the cachesize to avoid false sharing between the CPUs
 */
typedef struct __attribute__((aligned(CACHESIZE))) local {
	reserved_t reserved;
	last_free_t last_free;
} local_t;

/**
 * @brief atomically sets the preferred tree to given tree
 * @param self pointer to local object
 * @param pfn pfn of new tree
 * @param free_count count of free Frames
 * @param old_reservation the old reservation
 * @return ERR_RETRY if atomic operation fails
 *         ERR_OK on success
 */
int local_set_new_preferred_tree(local_t *self, uint64_t pfn,
				 uint16_t free_count,
				 reserved_t *old_reservation);

// init with no tree reserved
void local_init(local_t *self);

// returns true if there is a reserved Tree
static inline bool local_has_reserved_tree(local_t *const local)
{
	return (reserved_t){ load(&local->reserved.raw) }.has_reserved_tree;
}

// get the pfn of the reserved tree
uint64_t local_get_reserved_pfn(local_t *self);

// set the flag for searching and returns previous status to check if the reservation_in_progress-flag was already set
reserved_t local_mark_as_searching(local_t *self);
// reset the flag for searching
int local_unmark_as_searching(local_t *self);

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
int local_inc_counter(local_t *const self, const uint64_t frame,
		      const size_t order);

/**
 * @brief decrements the free counter by 2^order
 * returns ERR_OK on success
 * ERR_RETRY on atomic operation fail
 * ERR_MEMORY if counter value was not high enough
 */
int64_t local_dec_counter(local_t *const self, const size_t order);

// resets the reserved tree and returns the old reservation
int local_steal(local_t *const self, reserved_t *const old_reservation);

// sets last_free to tree of given frame
// returns ERR_OK on success
// ERR_Retry on atomic operation fail
// UPDATE_RESERVED after 4 consecutive free on the same tree
int local_set_free_tree(local_t *self, uint64_t frame);

/// updates the atomic index of last reserved tree
// returns ERR_OK on success
// ERR_RETRY on atomic operation fail
int local_update_last_reserved(local_t *const self, uint64_t pfn);

/// spin-wait until in-reservation flag becomes false
void local_wait_for_completion(const local_t *const self);
