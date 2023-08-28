#pragma once
#include <stdint.h>
#include "lower.h"
#include "child.h"
#include "local.h"
#include "tree.h"

#define MAX_ORDER 9
#define MIN_PAGES 1ul << 9         // minimum one HP
#define MAX_PAGES 1ul << 52        // 64 Bit Addresses - 12 Bit needed for offset inside the Page

typedef struct upper {
    struct meta* meta;
    lower_t lower;
    size_t cores;           //array_size of local
    struct local* local;
    size_t num_of_trees;    //array_size of trees
    tree_t* trees;
} upper_t;




/// Creates the allocator and returns a pointer to its data that is passed into
/// all other functions
void *llc_default();

/// Initializes the allocator for the given memory region, returning 0 on
/// success or a negative error code
//

/**
 * @brief Initializes the Allocator
 *
 * @param self pointer to allocator created by llc_default()
 * @param cores number of cores
 * @param start_frame_adr pointer to start of managed memory region
 * @param len length of managed memory region in number of 4k-Pages
 * @param init Init mode: VOLATILE:  allocator uses malloc for its own control structures
 *                        OVERWRITE: allocator uses parts of the managed memory for its own control structures
 *                        RECOVER:   allocator assumes a initializes allocator an will recover its state. if no old state is found it will return ERR_INITIALIZATION
 * @param all_free boolean value- if true all frames are free otherwise all frames will be initially allocated
 * @return int64_t
 */
int64_t llc_init(void *self, size_t cores, uint64_t start_frame_adr, size_t len,
                 uint8_t init, uint8_t all_free);

/// Allocates a frame and returns its address, or a negative error code
int64_t llc_get(const void *self, size_t core, size_t order);

/// Frees a frame, returning 0 on success or a negative error code
int64_t llc_put(const void *self, size_t core, uint64_t frame_adr,
                size_t order);

/// Checks if a frame is allocated, returning 0 if not
uint8_t llc_is_free(const void *self, uint64_t frame_adr, size_t order);

/// Returns the total number of frames the allocator can allocate
uint64_t llc_frames(const void *self);

/// Returns number of currently free frames
uint64_t llc_free_frames(const void *self);

/// Destructs the allocator
void llc_drop(void* self);

/// Prints the allocators state for debugging with given Rust printer
void llc_debug(const void *self, void (*writer)(void *, char *), void *arg);

/// Prints detailed stats about the allocator state
void llc_print(const upper_t* self);

/// Calls f for each Huge Frame. f will receive the context the current pfn and the free counter as arguments - used by some rust benchmarks like frag.rs
void llc_for_each_HP(const void *this, void* context, void f(void*, uint64_t, uint64_t));