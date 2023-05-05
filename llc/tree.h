#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

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


// TODO description
#define BREAKPOINT 0.125
typedef enum saturation_level{
  ALLOCATED,
  FREE,
  PARTIAL,
}saturation_level_t;


/**
 * @brief initializes the counter with the given values
 * it does so non-atomicly because at the time of creation there can be no
 * second access
 * @param counter initial counter Value must be < 0x8000 (fit in 15 bit)
 * @param flag initial flag value
 * @return initialized tree
 */
tree_t init_tree(uint16_t counter, bool flag);

/**
 * @brief atomically increases the counter
 * @param self pointer to the tree
 * @return ERR_OK on success
 *         ERR_RETRY if the atomic access failed
 *         ERR_MEMORY if the Counder already reached the maximum Value
 */
int tree_counter_inc(tree_t *self);

/**
 * @brief atomically decreases the counter
 * @param self pointer to the tree
 * @return ERR_OK on success
 *         ERR_RETRY if the atomic access failed
 *         ERR_MEMORY if the counter already reached the minimum Value
 */
int tree_counter_dec(tree_t *self);

/**
 * @brief atomically serts counter to 0 and flag as true
 * @param self pointer to the tree
 * @return  counter value on success
 *          ERR_RETRY if the atomic access failed
 *          ERR_ADRESS if already reserved
 */
int reserve_tree(tree_t *self);



/**
 * @brief evaluatex how many frames are allocates in given tree
 * @param self pointer to the tree
 * @return  ALLOCATED if most of the Frames are allocated
 *          FREE if most of the Frames are Free
 *          PARTIAL for everything in between
 */
saturation_level_t tree_status(tree_t* self);