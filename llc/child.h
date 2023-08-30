#pragma once

#include "utils.h"
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// child has 512 entry's
#define CHILDSIZE FIELDSIZE

/**
 * the raw value is for atomic access purpose
 * the counter and flag tags allow easy access to the fields.
 */
typedef struct child {
	union {
		_Atomic(uint16_t) raw;
		struct {
			uint16_t counter : 10;
			bool flag : 1;
			uint8_t unused : 5;
		};
	};
} child_t;

/**
 * @brief initializes the counter with the given values
 * @param counter initial counter Value must be < 0x400 (fit in 10 bit)
 * @param flag initial flag value
 * @return initialized child
 */
#define child_init(_counter, _flag) \
	({ (child_t){ ((_flag) << 10) | (_counter) }; })
/**
 * @brief same as atomic_counter_inc but only increments if the flag is not set
 * @param self pointer to the counter
 * @return ERR_OK on success
 *         ERR_ADDRESS if the Counter already reached the maximum Value
 */
int child_counter_inc(child_t *self);

/**
 * @brief same as atomic_counter_dec bit only decrements if the flag is not set.
 * @param self pointer to the counter
 * @return ERR_OK on success
 *         ERR_MEMORY if the counter already reached the minimum Value
 */
int child_counter_dec(child_t *self);

/**
 * @brief Reserves a Huge-Page by checking if all Frames are Free and setting
 * the Bit atomically.
 * @param self Pointer to the child
 * @return ERR_OK on success
 *         ERR_MEMORY if some Bits are already set (so no HP can be reserved)
 */
int child_reserve_HP(child_t *self);

/**
 * @brief Frees a Huge-Page by resetting the flag and setting the Counter to 512
 * atomically.
 * @param self Pointer to the child
 * @return ERR_OK on success
 *         ERR_ADDRESS if the child is not a reserved HP
 */
int child_free_HP(child_t *self);

/**
 * @brief atomically reads and returns the counter Value of the given child
 * @param self pointer to child
 * @return value of the counter
 */
static inline size_t child_get_counter(child_t *child_ptr)
{
	child_t child = { load(&(child_ptr)->raw) };
	return child.counter;
}

/**
 * @brief atomically checks weather this child is reserved as a HP
 * @param self pointer to child
 * @return true if child is a huge page
 */
static inline bool child_is_HP(child_t *child_ptr)
{
	child_t child = { load(&(child_ptr)->raw) };
	return child.flag;
}
