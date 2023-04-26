#pragma once

#include "pfn.h"
#include "bitfield.h"
#include "flag_counter.h"
#include <stdint.h>
#include "pfn.h"
#include <stdalign.h>

#define MAX_TREE_INDEX 0x3FFFFFFFFFFF // identifier if a tree is reserved.

 struct __attribute__((aligned(CACHESIZE))) local  { 
    struct reserved{
        union{
            _Atomic(uint64_t) raw;
            struct{
                uint16_t fcounter : 15;
                bool in_reservation: 1;
                uint64_t preferred_index : 46;
                uint8_t unused : 2;
            };
        };
    } reserved;
    struct last_free{
        union{
            _Atomic(uint64_t) raw;
            struct {
                uint16_t free_counter: 16; //TODO Bits reduzieren
                uint64_t last_free_idx: 46;
                uint8_t unused : 2;

            };
        };
    } last_free;
};
typedef struct local local_t;
typedef struct reserved reserved_t;
typedef struct last_free last_free_t;



/**
 * @brief atoicly sets the preferred tree to given tree
 * @param self pointer to local object
 * 
 *
 */
int set_preferred(local_t* self, pfn_rt pfn, uint16_t tree);

//init and set preferred to magic value
void init_local(local_t* self);
// check if it has a reserved tree
bool has_reserved_tree(local_t* self);
// get the reserved tree index
pfn_rt get_reserved_tree_index(local_t* self);
// set the flag for searching
int mark_as_searchig(local_t* self);
//check if already reserving a new tree
bool is_searching(local_t* self);
//set last free index
int inc_free_counter(local_t* self, pfn_at tree_index);