#pragma once
#include <stdint.h>
#include "lower.h"
#include "child.h"
#include "local.h"
#include "tree.h"
//Compilen und ausführen:
//cargo test -r -p nvalloc -- llc::test --nocapture

//cargo test upper::test::[testname] -- --nocapture
// testnamen siehe mod.rs
// nocapture für anzeige welche assertion fehlschlägt

#define MAX_ORDER 10
#define MIN_PAGES 1ul << 9        //TODO check amount - Min size needed for datastructure in persistend memory
#define MAX_PAGES 1ul << 52        // 64 Bit Adresses - 12 Bit needed for offset inside the Page

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

/// Prints detailed stats about the allokator state
void llc_print(const upper_t* self);

/// Calls f for each Huge Frame. f will recieve the context the currend pfn andt the freecounter as arguments
void llc_for_each_HP(const void *this, void* context, void f(void*, uint64_t, uint64_t));