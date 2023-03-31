#pragma once
#include <stdint.h>
#include <stdatomic.h>
//Compilen und ausführen:
//cargo test -r -p nvalloc -- llc::test --nocapture

enum {
  /// Success
  ERR_OK = 0,
  /// Not enough memory
  ERR_MEMORY = -1,
  /// Failed atomic operation, retry procedure
  ERR_RETRY = -2,
  /// Invalid address
  ERR_ADDRESS = -3,
  /// Allocator not initialized or initialization failed
  ERR_INITIALIZATION = -4,
  /// Corrupted allocator state
  ERR_CORRUPTION = -5,
};

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
