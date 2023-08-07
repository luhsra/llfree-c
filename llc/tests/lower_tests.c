#include "../lower.h"
#include "../bitfield.h"
#include "../lower.h"
#include "check.h"
#include "enum.h"
#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>

#define u64MAX 0xffffffffffffffff


#define check_child_number(expect)  \
    check_uequal_m(actual.num_of_childs, (expect), "schould have exactly 1 Child per 512 Frames");

#define bitfield_is_free(actual)    \
    check_equal_bitfield(actual, ((bitfield_t) {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}))
#define bitfield_is_blocked(actual) \
    check_equal_bitfield(actual, ((bitfield_t) {u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX}))

#define bitfield_is_free_n(actual, n)    \
    for(size_t i = 0; i < n; i++){  \
        bitfield_is_free(actual[i])    \
    }
#define bitfield_is_blocked_n(actual, n)    \
    for(size_t i = 0; i < n; i++){  \
        bitfield_is_blocked(actual[i])    \
    }

#define free_lower(lower)   \
    free(lower.fields);   \
    free(lower.childs);


bool init_lower_test(uint8_t init){
    bool success = true;

    uint64_t pfn_start = 0;
    int frames = 1024;
    lower_t actual;
    if(init != VOLATILE){
        pfn_start = (uint64_t) aligned_alloc(CACHESIZE, sizeof(char) * frames * PAGESIZE);
        assert(pfn_start != 0);
    }
    lower_init_default(&actual, pfn_start, frames, init);
    int ret = lower_init(&actual, true);
    check_equal(ret, ERR_OK);
    check_child_number(2ul);
    bitfield_is_free(actual.fields[0])
    check_uequal(lower_allocated_frames(&actual),0ul)
    if(init == VOLATILE){free_lower(actual);}

    frames = 1023;
    if(init != VOLATILE){
        free((char*) pfn_start);
        pfn_start = (uint64_t) aligned_alloc(CACHESIZE, sizeof(char) * frames * PAGESIZE);
        assert(pfn_start != 0);
    }else{
        pfn_start = 0;
    }
    lower_init_default(&actual, pfn_start, frames, init);
    ret = lower_init(&actual, true);
    check_child_number(2ul);
    check_equal_bitfield(actual.fields[1], ((bitfield_t) {0,0,0,0,0,0,0,init == VOLATILE ? 0x8000000000000000 : 0xC000000000000000}))
    check_uequal(lower_allocated_frames(&actual),0ul)
    if(init == VOLATILE){free_lower(actual);}

    frames = 632;
    if(init != VOLATILE){
        free((char*) pfn_start);
        pfn_start = (uint64_t) aligned_alloc(CACHESIZE, sizeof(char) * frames * PAGESIZE);
        assert(pfn_start != 0);
    }else{
        pfn_start = 0;
    }
    lower_init_default(&actual, pfn_start, frames, init);
    ret = lower_init(&actual, false);
    check_child_number(2ul);
    check_uequal(lower_allocated_frames(&actual),  init == VOLATILE ? 632ul : 631ul)
    if(init == VOLATILE){free_lower(actual);}


    frames = 685161;
    if(init != VOLATILE){
        free((char*) pfn_start);
        pfn_start = (uint64_t) aligned_alloc(CACHESIZE, sizeof(char) * frames * PAGESIZE);
        assert(pfn_start != 0);
    }else{
        pfn_start = 0;
    }
    lower_init_default(&actual, pfn_start, frames, init);
    ret = lower_init(&actual, true);
    check_child_number(1339ul);
    bitfield_is_free_n(actual.fields, 1338)
    check_equal_bitfield(actual.fields[1338], ((bitfield_t) {0x0, init == VOLATILE ? 0xfffffe0000000000 : 0xfffffffffff80000 ,u64MAX,u64MAX,u64MAX,u64MAX,u64MAX,u64MAX}))
    check_uequal(lower_allocated_frames(&actual),0ul)


    //check alignment

    check_uequal_m((uint64_t)actual.childs % CACHESIZE, 0ul , "array must be aligned to cachesize");
    check_uequal_m((uint64_t)actual.fields % CACHESIZE, 0ul , "array must be aligned to cachesize");

    if(init == VOLATILE){free_lower(actual);}

    return success;
}

bool get_test(){
    bool success = true;

    lower_t actual;
    lower_init_default(& actual, 0, 1360, VOLATILE);
    assert(lower_init(&actual, true) == ERR_OK);

    int ret;
    int order = 0;

    ret = lower_get(&actual,0,order);
    check_equal(ret, 0);
    check_equal_bitfield(actual.fields[0], ((bitfield_t) {0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}))

    ret = lower_get(&actual,0,order);
    check_equal(ret, 1);
    check_equal_bitfield(actual.fields[0], ((bitfield_t) {0x3, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}))


    ret = lower_get(&actual,320,order);
    check_equal(ret, 320);
    check_equal_bitfield(actual.fields[0], ((bitfield_t) {0x3, 0x0, 0x0, 0x0, 0x0, 0x1, 0x0, 0x0}))

    for(int i = 0; i < 954; i++){
        ret = lower_get(&actual,0,order);
        check_equal(ret, (i + (i < 318 ? 2 : 3)));
    }
    check_equal_bitfield(actual.fields[0], ((bitfield_t){u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX}))
    check_equal_bitfield(actual.fields[1], ((bitfield_t) {u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, 0x1fffffffffffffff, 0x0}))


    free_lower(actual)
    lower_init_default(&actual, 0, 2, VOLATILE);
    assert(lower_init(&actual, true) == ERR_OK);

    ret = lower_get(&actual,0,order);
    check_equal(ret, 0);
    check_equal_bitfield(actual.fields[0], ((bitfield_t) {0xfffffffffffffffd, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX}))

    ret = lower_get(&actual,0,order);
    check_equal(ret, 1);
    check_equal_bitfield(actual.fields[0], ((bitfield_t) {u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX}))

    ret = lower_get(&actual,0,order);
    check_equal(ret, ERR_MEMORY);
    check_equal_bitfield(actual.fields[0], ((bitfield_t) {u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX}))

    free_lower(actual)
    lower_init_default(&actual, 0, 166120, VOLATILE);
    assert(lower_init(&actual, true) == ERR_OK);

    ret = lower_get(&actual, 0,0);
    check(ret == 0, "");
    ret = lower_get(&actual, 0,HP_ORDER);
    check_equal(actual.childs[1].flag, true);
    check_equal(actual.childs[1].counter, 0);
    check_equal_bitfield(actual.fields[1], ((bitfield_t) {0, 0, 0, 0, 0, 0, 0, 0}))


    check_equal(ret, 1<<9);



    return success;
}


bool put_test(){
    bool success = true;

    lower_t actual;
    lower_init_default(&actual, 0, 1360, VOLATILE);
    assert(lower_init(&actual, true) == ERR_OK);

    uint64_t pfn;
    int ret;
    int order = 0;

    for(int i = 0; i < 957; i++){
        ret = lower_get(&actual,0,order);
        assert(ret >= 0);
    }
    check_equal_bitfield(actual.fields[0], ((bitfield_t){u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX}))
    check_equal_bitfield(actual.fields[1], ((bitfield_t) {u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, 0x1fffffffffffffff, 0x0}))

    pfn = 0;
    ret = lower_put(&actual, pfn, order);
    check_equal(ret, ERR_OK)
    check_equal_bitfield(actual.fields[0], ((bitfield_t){0xfffffffffffffffe, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX}))
    check_equal_bitfield(actual.fields[1], ((bitfield_t) {u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, 0x1fffffffffffffff, 0x0}))

    // wiederholtes put auf selbe stelle
    pfn = 0;
    ret = lower_put(&actual, pfn, order);
    check_equal(ret, ERR_ADDRESS)
    check_equal_bitfield(actual.fields[0], ((bitfield_t){0xfffffffffffffffe, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX}))
    check_equal_bitfield(actual.fields[1], ((bitfield_t) {u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, 0x1fffffffffffffff, 0x0}))

    pfn = 957;
    ret = lower_put(&actual, pfn, order);
    check_equal(ret, ERR_ADDRESS)
    check_equal_bitfield(actual.fields[0], ((bitfield_t){0xfffffffffffffffe, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX}))
    check_equal_bitfield(actual.fields[1], ((bitfield_t) {u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, 0x1fffffffffffffff, 0x0}))

    pfn = 561;
    ret = lower_put(&actual, pfn, order);
    check_equal(ret, ERR_OK)
    check_equal_bitfield(actual.fields[0], ((bitfield_t){0xfffffffffffffffe, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX}))
    check_equal_bitfield(actual.fields[1], ((bitfield_t) {0xfffdffffffffffff, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, 0x1fffffffffffffff, 0x0}))

    //größer als die größte pfn
    pfn = 1361;
    ret = lower_put(&actual, pfn, order);
    check_equal(ret, ERR_ADDRESS)
    check_equal_bitfield(actual.fields[0], ((bitfield_t){0xfffffffffffffffe, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX}))
    check_equal_bitfield(actual.fields[1], ((bitfield_t) {0xfffdffffffffffff, u64MAX, u64MAX, u64MAX, u64MAX, u64MAX, 0x1fffffffffffffff, 0x0}))

    free_lower(actual);
    return success;
}


bool is_free_test(){
    bool success = true;

    lower_t actual;
    lower_init_default(&actual, 0, 1360, VOLATILE);
    assert(lower_init(&actual, true) == ERR_OK);

    int ret;
    int order = 0;

    uint64_t pfn = 0;
    ret = lower_is_free(&actual, pfn, order);
    check_equal(ret, true);

    pfn = 910;
    ret = lower_is_free(&actual, pfn, order);
    check_equal(ret, true);

    free_lower(actual);

    lower_init_default(&actual, 0, 1360, VOLATILE);
    assert(lower_init(&actual, false) == ERR_OK);

    ret = lower_is_free(&actual, pfn, order);
    check_equal(ret, false);

    pfn = 910;
    ret = lower_is_free(&actual, pfn, order);
    check_equal(ret, false);
    assert(lower_put(&actual,513, order) == ERR_OK);
    assert(lower_put(&actual,511, order) == ERR_OK);
    ret = lower_is_free(&actual, 513, order);
    check_equal(ret, true);
    ret = lower_is_free(&actual, 511, order);
    check_equal(ret, true);
    ret = lower_is_free(&actual, 512, order);
    check_equal(ret, false);


    return success;
}


int lower_HP_tests(){
    bool success = true;

    lower_t actual;
    lower_init_default(&actual, 0, FIELDSIZE * 60, VOLATILE);
    assert(lower_init(&actual, true) == ERR_OK);

    int64_t pfn1 = lower_get(&actual, 0, HP_ORDER);
    check( pfn1 >= 0, "");
    uint64_t offset = pfn1 % FIELDSIZE;
    check_uequal(offset, 0ul);
    int64_t pfn2 = lower_get(&actual, 0, HP_ORDER);
    check( pfn2 >= 0, "");
    offset = pfn2 % FIELDSIZE;
    check_uequal(offset, 0ul);
    check(pfn1 != pfn2, "");
    check_uequal(lower_allocated_frames(&actual), 2ul * FIELDSIZE);

    // request a regular frame
    int64_t regular = lower_get(&actual, 0, 0);
    //regular frame cannot be returned as HP
    check_equal(lower_put(&actual, regular, HP_ORDER), ERR_ADDRESS);


    assert(regular >= 0);
    //this HF must be in another child than the regular frame.
    int64_t pfn3 = lower_get(&actual, pfn_from_atomic(10), HP_ORDER);
    check(pfn3 >= 0,"");
    offset = pfn3 % FIELDSIZE;
    check_uequal(offset, 0ul);
    check_uequal((uint64_t)pfn3, 3*FIELDSIZE + actual.start_frame_adr);

    // free regular page und try get this child as complete HP
    assert(lower_put(&actual, regular, 0) == ERR_OK);
    int64_t pfn4 = lower_get(&actual, 0, HP_ORDER);
    check(pfn4 >= 0,"");
    check(pfn4 == regular, "");


    int ret = lower_put(&actual, pfn2, HP_ORDER);
    check_equal(ret, ERR_OK);

    //allocate the complete memory with HPs
    for(int i = 3; i < 60; ++i){
        // get allocates only in chunks of 32 children. if there is no free HP in given chung it returns ERR_MEMORY
        int64_t pfn = lower_get(&actual, i < 32? 0 : 32*FIELDSIZE, HP_ORDER);
        check(pfn > 0, "");
    }

    check_uequal_m(lower_allocated_frames(&actual), actual.length, "fully allocated with Huge Frames");

    //reservation at full memory must fail
    int64_t pfn = lower_get(&actual, 0, HP_ORDER);
    check(pfn == ERR_MEMORY,"");

    // return HP as regular Frame must succseed
    check(lower_put(&actual, pfn2, 0) == ERR_OK, "");
    // HP ist converted into regular frames so returning the whole page must fail
    check(lower_put(&actual, pfn2, HP_ORDER) == ERR_ADDRESS, "");

    check(lower_put(&actual, pfn1, HP_ORDER) == ERR_OK, "");

    //check if right amout of free regular frames are present
    check_uequal(actual.length - lower_allocated_frames(&actual), FIELDSIZE + 1ul);

    //new aquired frame should be in same positon as the old no 1
    check(lower_get(&actual, 0, HP_ORDER) == pfn1, "");


    return success;
}

int init_persistent_test(){
    return init_lower_test(OVERWRITE);
}

int init_volatile_test(){
    return init_lower_test(VOLATILE);
}


int free_all_test(){
    bool success = true;
    const uint64_t len = (1 << 13) + 35; //16 HP + 35 regular frames
    char* memory = aligned_alloc(CACHESIZE, PAGESIZE * len);
    assert(memory != NULL);
    const uint64_t start_adr = (uint64_t) memory;

    lower_t lower;
    lower_init_default(&lower, start_adr, len, OVERWRITE);
    int ret = lower_init(&lower, false);
    assert(ret == ERR_OK);
    check_uequal_m(lower.length, len -1, "one frame for management structures");
    check_uequal_m(lower.length - lower_allocated_frames(&lower), 0ul, "all_frames_are_allocated");

    //free all HPs
    for(int i = 0; i < 15; ++i){
        ret = lower_put(&lower, start_adr + i*512, HP_ORDER);
        check_equal(ret, ERR_OK);
    }
    check_uequal_m(lower_allocated_frames(&lower), 512ul + 34, "one allocated HF and the 34 regular frames");

    // free last HP as regular frame and regular frames
    const uint64_t start = start_adr + 15 * 512;
    for(int i = 0; i < 512 + 34; ++i){
        ret = lower_put(&lower, start + i, 0);
        check_equal(ret, ERR_OK);
    }

    check_uequal_m(lower_allocated_frames(&lower), 0ul, "lower should be completely free");

    free(memory);
    return success;
}




//runns all tests an returns the number of failed Tests
int lower_tests(int* test_counter, int* fail_counter){

    run_test(init_volatile_test);
    run_test(init_persistent_test)
    run_test(get_test);
    run_test(put_test);
    run_test(is_free_test);
    run_test(lower_HP_tests);
    run_test(free_all_test);
    return 0;
}