#include "utils.h"
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#define TREESIZE (1 << 9 << 5)
/**
 * the raw value is for atomic access purpose and for allignment
 * the counter and flag tags allow easy access to the components.
 */
typedef struct tree {
  union {
    alignas(2) _Atomic(uint16_t) raw;
    struct {
      uint16_t counter : 15;
      bool flag : 1;
    };
  };
} tree_t;

// Saturation level for frames
typedef enum saturation_level {
  ALLOCATED, // most of the fames are allocated
  FREE,      // only a few frames are allocated
  PARTIAL,   // anything in between
} saturation_level_t;

/**
 * @brief initializes the counter with the given values
 * it does so non-atomicly because at the time of creation there can be no
 * second access
 * @param counter initial counter Value must be < 0x8000 (fit in 15 bit)
 * @param flag initial flag value
 * @return initialized tree
 */
tree_t tree_init(uint16_t counter, bool flag);

/**
 * @brief atomically increases the counter
 * @param self pointer to the tree
 * @return ERR_OK on success
 *         ERR_RETRY if the atomic access failed
 */
int tree_counter_inc(tree_t *self, size_t order);

/**
 * @brief atomically decreases the counter
 * @param self pointer to the tree
 * @return ERR_OK on success
 *         ERR_RETRY if the atomic access failed
 */
int tree_counter_dec(tree_t *self, size_t order);

/**
 * @brief atomically serts counter to 0 and flag as true
 * @param self pointer to the tree
 * @return  counter value on success
 *          ERR_RETRY if the atomic access failed
 *          ERR_ADRESS if already reserved
 */
int tree_reserve(tree_t *self);

/**
 * @brief adds the given counter to the existing counter and sets the flag to 0
 *
 * @param self pointer to tree
 * @param free_counter counter to add
 * @return ERR_OK on success
 *         ERR_RETRY on atomic operration fail
 */
int tree_writeback(tree_t *self, uint16_t free_counter);

/**
 * @brief merges the counters of both trees and returns the value. Tree stays
 * reserved
 *
 * @param self
 * @param free_counter
 * @return int
 */
int tree_writeback_and_reserve(tree_t *self, uint16_t free_counter);

/**
 * @brief evaluatex how many frames are allocates in given tree
 * @param self pointer to the tree
 * @return  ALLOCATED if most of the Frames are allocated or the tree is
 * reserved FREE if most of the Frames are Free PARTIAL for everything in
 * between
 */
saturation_level_t tree_status(const tree_t *self);

// steals the counter of a reserved tree
int tree_steal_counter(tree_t *self);