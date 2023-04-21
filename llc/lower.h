#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "flag_counter.h"
#include "bitfield.h"
#include "pfn.h"

//TODO Nice Function Descriptions

typedef struct lower{
    pfn_t start_pfn;
    uint64_t length;
    size_t num_of_childs;   //arraylenght for fields and childs
    bitfield_512_t* fields;
    flag_counter_t* childs;
}lower_t;


/**
 * @brief allocates memory for the bitfields and flagcounters
 * @param self pointer to the lower object
 * @param start_pfn currently ununsed
 * @param len amount of frames to be managed
 */
void init_default(lower_t* self, pfn_t start_pfn, uint64_t len);

/**
 * @brief initialize the lower object and the bitfields and childs
 * works non-atomically because is expectet not to run paralell
 * @param self pointer to lower objekt
 * @param start_pfn number of the first managed Page-Frame
 * @param len amount of consecutive frames to be managed
 * @param free_all if set all the space will be marked allocated at start. (free otherwise)
 * @return ERR_OK
 */
int init_lower(lower_t* self, pfn_t start_pfn, uint64_t len, bool free_all);

/**
 * @brief allocates frames
 * allocation will be searched in a chunk of 32 children.
 * @param self pointer to lower object
 * @param start defines the chunk to be searched (will start at start of the chunk even when start points to the middle)
 * @param order determines the amount consecutive pages to be alloced (2^order)
 * @param ret will be set to the pfn of the first of the new allocated pages.
 * @return ERR_OK in success
 *         ERR_MEMORY if not enough space was found (ret will be undefined)
 */
int get(lower_t* self, size_t start, size_t order, pfn_t* ret);

/**
 * @brief deallocates given frames
 * @param self pointer to lower object
 * @param frame Number of the first PageFrame to be freed
 * @param order determines the amount consecutive pages to be freed (2^order)
 * @return ERR_OK in success
 *         ERR_ADDRESS if the pointed to frames were not alloced
 */
int put(lower_t* self, pfn_t frame, size_t order);

/**
 * @brief checks if the memory location is free
 * @param self pointer lo lower objekt
 * @param frame PFN of the first Frame
 * @param order determines the amount consecutive pages to check for be free (2^order)
 * @return true if the pages pointed to are free
 *         false otherwise
 */
int is_free(lower_t* self, pfn_t frame, size_t order);

/**
 * @brief calculates the number of allocated Frames
 * it uses the values from the child-counters
 * @param self pointer lo lower objekt
 * @return number of allocated frames
 */
uint64_t allocated_frames(lower_t* self);

/**
* Helper to print the number of childen, allocated and managed Frames
*/
void print_lower(lower_t* self);

