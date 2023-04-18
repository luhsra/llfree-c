#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <stdatomic.h>
#include "enum.h"

/**
 * This Flagcounter combines a Flag with a 15-Bit counter in 2 Bytes of Space.
 * It Provides Functions to check and set these atomically.
 * 
*/


#define MAX_ATOMIC_RETRY 5  // Maximum retrys if a atomic operation has failed


/**
 * the raw value is for atomic access purpose and for allignment
 * the counter and flag tags allow easy access to the components.
 */
typedef struct flag_counter{
    union{
        alignas(2) _Atomic(uint16_t) raw;
        struct{
            uint16_t counter:15;
            bool flag :1;
        };
    };
} flag_counter_t;

/**
 * @brief initializes the counter with the given values
 * it does so non-atomicly because at the time of creation there can be no second access
 * @param counter initial counter Value must be < 0x8000 (fit in 15 bit)
 * @param flag initial flag value
 * @return initialized flagcounter
 */
flag_counter_t init_flag_counter(uint16_t counter, bool flag);

/**
 * @brief Reserves a Huge-Page by checking if all Frames are Free and setting the Bit atomically.
 * @param self Pointer to the flagcounter
 * @return ERR_OK on success
 *         ERR_MEMORY if some Bits are alredy set (so no HP can be reserved)
 */
int reserve_HP(flag_counter_t* self);

/**
 * @brief Frees a Huge-Page by resetting the flag and setting the Counter to 512 atomically.
 * @param self Pointer to the flagcounter
 * @return ERR_OK on success
 *         ERR_ADRESS if the flagcounter is not a reserved HP
 *         ERR_RETRY if atomic Fails (should never be the case, because there schould not be a competition for access)
 */
int free_HP(flag_counter_t* self);

/**
 * @brief atomically increases the counter
 * @param self pointer to the counter
 * @return ERR_OK on success
 *         ERR_RETRY if the atomic access failed
 *         ERR_MEMORY if the Counder already reached the maximum Value
 */
int atomic_counter_inc(flag_counter_t* self);

/**
 * @brief same as atomic_counter_inc but only increments if the flag is not set
 * @param self pointer to the counter
 * @return ERR_OK on success
 *         ERR_RETRY if the atomic access failed
 *         ERR_MEMORY if the Counder already reached the maximum Value
 *         ERR_CORRUPTION if flag was set
 */
int child_counter_inc(flag_counter_t* self);

/**
 * @brief atomically decreases the counter
 * @param self pointer to the counter
 * @return ERR_OK on success
 *         ERR_RETRY if the atomic access failed
 *         ERR_MEMORY if the counter already reached the minimum Value
 */
int atomic_counter_dec(flag_counter_t* self);

/**
 * @brief same as atomic_counter_dec bit only decrements if the flag is not set.
 * @param self pointer to the counter
 * @return ERR_OK on success
 *         ERR_RETRY if the atomic access failed
 *         ERR_MEMORY if the counter already reached the minimum Value
 *         ERR_ADDRESS if the flag was is set
 */
int child_counter_dec(flag_counter_t* self);

/**
 * @brief atomically reads and returns the raw Value of the given flagcounter
 * @param self flagcounter to read
 * @return raw value of the flagcounter
 */
uint16_t get_counter(flag_counter_t* self);


