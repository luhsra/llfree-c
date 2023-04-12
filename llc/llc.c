#include "llc.h"
#include "enum.h"
//Compilen und ausführen:
//cargo test -r -p nvalloc -- llc::test --nocapture
  

/// Creates the allocator and returns a pointer to its data that is passed into
/// all other functions
void *llc_default() { return 0; }

/// Initializes the allocator for the given memory region, returning 0 on
/// success or a negative error code
int64_t llc_init(void *this, uint64_t cores, uint64_t start_pfn, uint64_t len,
                 uint8_t init, uint8_t free_all) {
  (void)(this);
  (void)(cores);
  (void)(start_pfn);
  (void)(len);
  (void)(init);
  (void)(free_all);
  return ERR_CORRUPTION;
}

/// Allocates a frame and returns its address, or a negative error code
int64_t llc_get(const void *this, uint64_t core, uint64_t order) {
  (void)(this);
  (void)(core);
  (void)(order);
  return ERR_CORRUPTION;
}

/// Frees a frame, returning 0 on success or a negative error code
int64_t llc_put(const void *this, uint64_t core, uint64_t frame,
                uint64_t order) {
  (void)(this);
  (void)(core);
  (void)(frame);
  (void)(order);
  return ERR_CORRUPTION;
}

/// Checks if a frame is allocated, returning 0 if not
uint8_t llc_is_free(const void *this, uint64_t frame, uint64_t order) {
  (void)(this);
  (void)(frame);
  (void)(order);
  return 0;
}

/// Returns the total number of frames the allocator can allocate
uint64_t llc_frames(const void *this) {
  (void)(this);
  return 0;
}

/// Returns number of currently free frames
uint64_t llc_free_frames(const void *this) {
  (void)(this);
  return 0;
}

/// Prints the allocators state for debugging
void llc_debug(const void *this, void (*writer)(void *, char *), void *arg) {
  (void)(this);
  writer(arg, "Hello from LLC!\n");
  writer(arg, "Can be called multiple times...");
}
