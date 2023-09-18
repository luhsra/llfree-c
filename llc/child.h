#pragma once

#include "utils.h"

// child has 512 entry's
#define CHILDSIZE FIELDSIZE

/**
 * the raw value is for atomic access purpose
 * the counter and flag tags allow easy access to the fields.
 */
typedef struct child {
	uint16_t counter : 15;
	bool huge : 1;
} child_t;

/**
 * @brief initializes the counter with the given values
 * @param counter initial counter Value must be < 0x400 (fit in 10 bit)
 * @param flag initial flag value
 * @return initialized child
 */
static inline child_t _unused child_new(uint16_t counter, bool flag)
{
	return (child_t){ .counter = counter, .huge = flag };
}

/**
 * @brief same as atomic_counter_inc but only increments if the flag is not set
 * @param self pointer to the counter
 * @return ERR_OK on success
 *         ERR_ADDRESS if the Counter already reached the maximum Value
 */
bool child_counter_inc(child_t *self, _void v);

/**
 * @brief same as atomic_counter_dec bit only decrements if the flag is not set.
 * @param self pointer to the counter
 * @return ERR_OK on success
 *         ERR_MEMORY if the counter already reached the minimum Value
 */
bool child_counter_dec(child_t *self, _void v);

/**
 * @brief Reserves a Huge-Page by checking if all Frames are Free and setting
 * the Bit atomically.
 * @param self Pointer to the child
 * @return ERR_OK on success
 *         ERR_MEMORY if some Bits are already set (so no HP can be reserved)
 */
bool child_free_huge(child_t *self, _void v);

/**
 * @brief Frees a Huge-Page by resetting the flag and setting the Counter to 512
 * atomically.
 * @param self Pointer to the child
 * @return ERR_OK on success
 *         ERR_ADDRESS if the child is not a reserved HP
 */
bool child_reserve_huge(child_t *self, _void v);
