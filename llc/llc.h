#pragma once
#include <stdint.h>
#include <stdatomic.h>
#include "lower.h"
#include "child.h"
#include "local.h"
#include "tree.h"
//Compilen und ausführen:
//cargo test -r -p nvalloc -- llc::test --nocapture

#define MAX_ORDER 10
#define MIN_PAGES 1ul << 9        //WHY?
#define MAX_PAGES 1ul << 52  //TODO Nachrechnen


struct meta {
    uint32_t magic;
    size_t frames;
    bool crashed;
};

typedef struct upper {
    struct meta* meta;
    lower_t lower;
    size_t cores;   //array_size of local
    struct local* local;
    size_t num_of_trees;    //array_size of trees
    tree_t* trees;
} upper_t;




/// Creates the allocator and returns a pointer to its data that is passed into
/// all other functions
void *llc_default();

/// Initializes the allocator for the given memory region, returning 0 on
/// success or a negative error code
int64_t llc_init(void *self, size_t cores, pfn_at start_pfn, size_t len,
                 uint8_t init, uint8_t free_all);

/// Allocates a frame and returns its address, or a negative error code
int64_t llc_get(const void *self, size_t core, size_t order);

/// Frees a frame, returning 0 on success or a negative error code
int64_t llc_put(const void *self, size_t core, pfn_at frame,
                size_t order);

/// Checks if a frame is allocated, returning 0 if not
uint8_t llc_is_free(const void *self, pfn_at frame, size_t order);

/// Returns the total number of frames the allocator can allocate
pfn_at llc_frames(const void *self);

/// Returns number of currently free frames
pfn_at llc_free_frames(const void *self);

/// Destructs the allocator
void llc_drop(void* self);

/// Prints the allocators state for debugging
void llc_debug(const void *self, void (*writer)(void *, char *), void *arg);
