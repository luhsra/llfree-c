#pragma once

#include "bitfield.h"
#include "child.h"
#include "utils.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHILDS_PER_TREE 32
#define PAGESIZE (1 << 12)

typedef struct lower {
	uint64_t start_frame_adr; // first address of managed space
	size_t length; // number of managed frames
	size_t childs_len; // array length for fields and childs
	bitfield_t *fields;
	_Atomic(child_t) *childs;
} lower_t;

/**
 * @brief setups the lower by initializing the pointer
 * if volatile_mem is set it will malloc the needed space otherwise the pointer will be set into the given memory.
 * @param self pointer to the lower object
 * @param start_pfn first address of managed space
 * @param len amount of frames to be managed
 * @param init Init mode: VOLATILE:  allocator uses malloc for its own control structures
 *                        OVERWRITE: allocator uses parts of the managed memory for its own control structures
 *                        RECOVER:   allocator assumes a initializes allocator an will recover its state.
  */
void lower_init_default(lower_t *const self, uint64_t start_adr, size_t len,
			uint8_t init);

/**
 * @brief initialize the lower object and the bitfields and childs
 * works non-atomically
 * @param self pointer to lower object
 * @param free_all if set all the space will be marked free at start. (allocated otherwise)
 * @return ERR_OK
 */
result_t lower_init(lower_t const *const self, bool free_all);

/**
 * @brief Recovers the state from persistent memory
 * checks and possibly corrects the free counter in childs
 * @param self pointer to lower allocator
 * @return ERR_OK
 */
result_t lower_recover(lower_t *self);

/**
 * @brief allocates frames
 * allocation will be searched in a chunk of CHILDS_PER_TREE children.
 * @param self pointer to lower object
 * @param pfn defines the chunk to be searched (will start at start of the
 * chunk even when start points to the middle)
 * @param order determines the amount consecutive pages to be alloced (2^order)
 * @return frame address of reserved frame on success;
 *         ERR_MEMORY if not enough space was found (ret will be undefined)
 */
result_t lower_get(lower_t const *const self, uint64_t pfn, size_t order);

/**
 * @brief deallocates given frame
 * @param self pointer to lower object
 * @param frame_adr Number of the PageFrame to be freed
 * @param order determines the amount consecutive pages to be freed (2^order)
 * @return ERR_OK in success
 *         ERR_ADDRESS if the pointed to frames were not alloced
 */
result_t lower_put(lower_t const *const self, uint64_t frame_adr, size_t order);

/**
 * @brief checks if the memory location is free
 * @param self pointer lower object
 * @param frame PFN of the first Frame
 * @param order determines the amount consecutive pages to check for be free
 * (2^order)
 * @return true if the pages pointed to are free
 *         false otherwise
 */
bool lower_is_free(lower_t const *const self, uint64_t frame_adr, size_t order);

/**
 * @brief calculates the number of allocated Frames
 * it uses the values from the child-counters
 * @param self pointer lo lower object
 * @return number of allocated frames
 */
size_t lower_allocated_frames(lower_t const *const self);

//returns the number of free huge frames
size_t lower_free_HPs(lower_t const *const self);

/**
 * Helper to print the number of children, allocated and managed Frames
 */
void lower_print(lower_t const *const self);

/**
 * @brief Frees the allocated Memory
 *
 * @param self pointer to lower
 */
void lower_drop(lower_t const *const self);

/// Calls f for each child. f will receive the context the current pfn and the free counter as arguments
// used by frag.rs benchmark
void lower_for_each_child(const lower_t *self, void *context,
			  void f(void *, uint64_t, uint64_t));
