#include "utils.h"
#include "assert.h"
#include "enum.h"
#include "flag_counter.h"
#include <stdatomic.h>
#include <stdint.h>

//TODO testcases

size_t div_ceil(uint64_t a, int b){
    //wenn es einen Rest gibt muss aufgerundet werden
    return a % b ? a / b + 1 : a / b;
}

#if false
//allways try next no change in desire
bool update16_default(uint16_t* current, uint16_t* desire){
    return true;
}

bool update64_default(uint64_t* current, uint64_t* desire){
    return true;
}
#endif



int cas16_complete(_Atomic(uint16_t)* obj, uint16_t* expect, bool (*update)(uint16_t* expect, uint16_t* desire)){
    assert(obj != NULL);
    assert(expect != NULL);
    assert(update != NULL);


    uint16_t desire;
    for(size_t i = 0; i < MAX_ATOMIC_RETRY; ++i){
        if(!update(expect, &desire)) return ERR_CANCEL;

        int ret = atomic_compare_exchange_strong(obj, expect, desire);
        if(ret) return ERR_OK;
    }
    return ERR_RETRY;
}

int cas16_update(_Atomic(uint16_t)* obj, bool update(uint16_t* expect, uint16_t* desire)){
    assert(obj != NULL);
    assert(update != NULL);

    uint16_t expect = atomic_load(obj);
    return cas16_complete(obj,&expect, update);
}

int cas64_complete(_Atomic(uint64_t)* obj, uint64_t* expect, bool (*update)(uint64_t* expect, uint64_t* desire)){
    assert(obj != NULL);
    assert(expect != NULL);
    assert(update != NULL);

    uint64_t desire;
    for(size_t i = 0; i < MAX_ATOMIC_RETRY; ++i){
        if(!update(expect, &desire)) return ERR_CANCEL;

        int ret = atomic_compare_exchange_strong(obj, expect, desire);
        if(ret) return ERR_OK;
    }
    return ERR_RETRY;
}

int cas64_update(_Atomic(uint64_t)* obj, bool (*update)(uint64_t* expect, uint64_t* desire)){
    assert(obj != NULL);
    assert(update != NULL);

    uint64_t expect = atomic_load(obj);
    return cas64_complete(obj,&expect, update);
}