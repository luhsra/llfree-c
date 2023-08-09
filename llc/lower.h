#pragma once

#include "bitfield.h"
#include "child.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CHILDS_PER_TREE 32
#define PAGESIZE (1 << 12)


typedef struct lower {
  uint64_t start_frame_adr;
  size_t length;
  size_t num_of_childs; // arraylenght for fields and childs
  bitfield_t *fields;
  child_t *childs;
} lower_t;

/**
 * @brief setups the lower by initializing the pointer
 * if volatile_mem is set it will malloc the needed space otherwise the pointer will be set into the given memory.
 * @param self pointer to the lower object
 * @param start_pfn currently ununsed
 * @param len amount of frames to be managed
 * @param volatile_mem decides where space for the datastructures are allocated
  */
void lower_init_default(lower_t *const self, uint64_t start_pfn, size_t len, uint8_t init);

/**
 * @brief initialize the lower object and the bitfields and childs
 * works non-atomically because is expectet not to run paralell
 * @param self pointer to lower objekt
 * @param start_pfn number of the first managed Page-Frame
 * @param len amount of consecutive frames to be managed
 * @param free_all if set all the space will be marked allocated at start. (free
 * otherwise)
 * @return ERR_OK
 */
int lower_init(lower_t const *const self, bool free_all);


/**
 * @brief Recovers the state from persistend memory
 * checks and possibly corrects the freecounter in childs
 * @param self pointer to lower allocator
 * @return ERR_OK
 */
int lower_recover(lower_t* self);

/**
 * @brief allocates frames
 * allocation will be searched in a chunk of CHILDS_PER_TREE children.
 * @param self pointer to lower object
 * @param pfn defines the chunk to be searched (will start at start of the
 * chunk even when start points to the middle)
 * @param order determines the amount consecutive pages to be alloced (2^order)
 * @return absolute pfn on success;
 *         ERR_MEMORY if not enough space was found (ret will be undefined)
 */
int64_t lower_get(lower_t const *const self, uint64_t pfn, size_t order);

/**
 * @brief deallocates given frames
 * @param self pointer to lower object
 * @param frame Number of the first PageFrame to be freed
 * @param order determines the amount consecutive pages to be freed (2^order)
 * @return ERR_OK in success
 *         ERR_ADDRESS if the pointed to frames were not alloced
 */
int lower_put(lower_t const *const self, uint64_t frame_adr, size_t order);

/**
 * @brief checks if the memory location is free
 * @param self pointer lower objekt
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
 * @param self pointer lo lower objekt
 * @return number of allocated frames
 */
size_t lower_allocated_frames(lower_t const *const self);

size_t lower_free_HPs(lower_t const * const self);

/**
 * Helper to print the number of childen, allocated and managed Frames
 */
void lower_print(lower_t const *const self);

/**
 * @brief Frees the allocated Memory
 *
 * @param self pointer to lower
 */
void lower_drop(lower_t const *const self);

/// Calls f for each child. f will recieve the context the currend pfn andt the freecounter as arguments
void lower_for_each_child(const lower_t *self, void* context, void f(void*, uint64_t, uint64_t));
