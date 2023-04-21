#pragma once
#include <bits/stdint-uintn.h>
#include <stdint.h>
#include <stdatomic.h>
#include "lower.h"
#include "flag_counter.h"
//Compilen und ausführen:
//cargo test -r -p nvalloc -- llc::test --nocapture

#define MAX_ORDER 10
#define MIN_PAGES 1ul << 9        //WHY?
#define MAX_PAGES 1ul << (9 * 4)  //TODO Nachrechnen


struct meta {
    uint32_t magic;
    size_t frames;
    bool crashed;
};

typedef struct local { //TODO rediesign with atomic in mind
    flag_counter_t copy;
    uint16_t start_pfn; // zu wenig speicher?
    uint16_t free_counter;
    uint16_t last_free_idx;

}local_t;

typedef struct upper {
    struct meta* meta;
    lower_t lower;
    size_t cores;   //array_size of local
    struct local* local;
    size_t num_of_trees;    //array_size of trees
    flag_counter_t* trees;
} upper_t;




/// Creates the allocator and returns a pointer to its data that is passed into
/// all other functions
void *llc_default();

/// Initializes the allocator for the given memory region, returning 0 on
/// success or a negative error code
int64_t llc_init(void *self, uint64_t cores, uint64_t start_pfn, uint64_t len,
                 uint8_t init, uint8_t free_all);

/// Allocates a frame and returns its address, or a negative error code
int64_t llc_get(const void *self, uint64_t core, uint64_t order);

/// Frees a frame, returning 0 on success or a negative error code
int64_t llc_put(const void *self, uint64_t core, uint64_t frame,
                uint64_t order);

/// Checks if a frame is allocated, returning 0 if not
uint8_t llc_is_free(const void *self, uint64_t frame, uint64_t order);

/// Returns the total number of frames the allocator can allocate
uint64_t llc_frames(const void *self);

/// Returns number of currently free frames
uint64_t llc_free_frames(const void *self);

/// Prints the allocators state for debugging
void llc_debug(const void *self, void (*writer)(void *, char *), void *arg);
