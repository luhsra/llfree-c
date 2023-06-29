#include "../lower.h"
#include "../bitfield.h"
#include "../lower.h"
#include "check.h"
#include "enum.h"
#include "pfn.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#define check_child_number(expect)  \
    check_uequal_m(actual.num_of_childs, expect, "schould have exactly 1 Child per 512 Frames");

#define bitfield_is_free(actual)    \
    check_equal_bitfield(actual, ((bitfield_512_t) {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}))
#define bitfield_is_blocked(actual) \
    check_equal_bitfield(actual, ((bitfield_512_t) {0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))

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


bool init_lower_test(){
    bool success = true;

    int pfn_start = 0;
    int frames = 512;
    lower_t actual;
    init_default(&actual, pfn_start, frames);
    int ret = init_lower(&actual, true);
    check_equal(ret, ERR_OK);
    check_child_number(1ul);
    bitfield_is_free(actual.fields[0])
    check_uequal(allocated_frames(&actual),0ul)
    free_lower(actual)

    pfn_start = 0;
    frames = 511;
    init_default(&actual, pfn_start, frames);
    ret = init_lower(&actual, true);
    check_child_number(1ul);
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t) {0,0,0,0,0,0,0,0x8000000000000000}))
    check_uequal(allocated_frames(&actual),0ul)
    free_lower(actual)

    pfn_start = 0;
    frames = 632;
    init_default(&actual, pfn_start, frames);
    ret = init_lower(&actual, false);
    check_child_number(2ul);
    bitfield_is_blocked_n(actual.fields,2)
    check_uequal(allocated_frames(&actual),632ul)
    free_lower(actual)

    pfn_start = 0;
    frames = 968;
    init_default(&actual, pfn_start, frames);
    ret = init_lower(&actual, true);
    check_child_number(2ul);
    bitfield_is_free(actual.fields[0])
    check_equal_bitfield(actual.fields[1], ((bitfield_512_t) {0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xffffffffffffff00}))
    check_uequal(allocated_frames(&actual),0ul)
    free_lower(actual)


    pfn_start = 0;
    frames = 685161;
    init_default(&actual, pfn_start, frames);
    ret = init_lower(&actual, true);
    check_child_number(1339ul);
    bitfield_is_free_n(actual.fields, 1338)
    check_equal_bitfield(actual.fields[1338], ((bitfield_512_t) {0x0,0xfffffe0000000000,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff}))
    check_uequal(allocated_frames(&actual),0ul)
    free_lower(actual)


    return success;
}

bool get_test(){
    bool success = true;

    lower_t actual;
    init_default(& actual, 0, 1360);
    assert(init_lower(&actual, true) == ERR_OK);

    int ret;
    int order = 0;

    ret = lower_get(&actual,0,order);
    check_equal(ret, 0);
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t) {0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}))

    ret = lower_get(&actual,0,order);
    check_equal(ret, 1);
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t) {0x3, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}))


    ret = lower_get(&actual,getTreeIdx(320),order);
    check_equal(ret, 2);
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t) {0x7, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}))

    for(int i = 0; i < 954; i++){
        ret = lower_get(&actual,0,order);
        check_equal(ret, (i + 3));
    }
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t){0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))
    check_equal_bitfield(actual.fields[1], ((bitfield_512_t) {0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0x1fffffffffffffff, 0x0}))


    free_lower(actual)
    init_default(&actual, 0, 2);
    assert(init_lower(&actual, true) == ERR_OK);

    ret = lower_get(&actual,0,order);
    check_equal(ret, 0);
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t) {0xfffffffffffffffd, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))

    ret = lower_get(&actual,0,order);
    check_equal(ret, 1);
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t) {0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))

    ret = lower_get(&actual,0,order);
    check_equal(ret, ERR_MEMORY);
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t) {0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))

    free_lower(actual)
    init_default(&actual, 0, 166120);
    assert(init_lower(&actual, true) == ERR_OK);

    ret = lower_get(&actual, 0,0);
    check(ret == 0, "");
    ret = lower_get(&actual, 0,HP);
    check_equal(actual.childs[1].flag, true);
    check_equal(actual.childs[1].counter, 0);
    check_equal_bitfield(actual.fields[1], ((bitfield_512_t) {0, 0, 0, 0, 0, 0, 0, 0}))


    check_equal(ret, 1<<9);



    return success;
}


bool put_test(){
    bool success = true;

    lower_t actual;
    init_default(&actual, 0, 1360);
    assert(init_lower(&actual, true) == ERR_OK);

    pfn_at pfn;
    int ret;
    int order = 0;

    for(int i = 0; i < 957; i++){
        ret = lower_get(&actual,0,order);
        assert(ret >= 0);
    }
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t){0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))
    check_equal_bitfield(actual.fields[1], ((bitfield_512_t) {0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0x1fffffffffffffff, 0x0}))

    pfn = 0;
    ret = lower_put(&actual, pfn, order);
    check_equal(ret, ERR_OK)
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t){0xfffffffffffffffe, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))
    check_equal_bitfield(actual.fields[1], ((bitfield_512_t) {0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0x1fffffffffffffff, 0x0}))

    // wiederholtes put auf selbe stelle
    pfn = 0;
    ret = lower_put(&actual, pfn, order);
    check_equal(ret, ERR_ADDRESS)
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t){0xfffffffffffffffe, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))
    check_equal_bitfield(actual.fields[1], ((bitfield_512_t) {0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0x1fffffffffffffff, 0x0}))

    pfn = 957;
    ret = lower_put(&actual, pfn, order);
    check_equal(ret, ERR_ADDRESS)
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t){0xfffffffffffffffe, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))
    check_equal_bitfield(actual.fields[1], ((bitfield_512_t) {0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0x1fffffffffffffff, 0x0}))
    
    pfn = 561;
    ret = lower_put(&actual, pfn, order);
    check_equal(ret, ERR_OK)
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t){0xfffffffffffffffe, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))
    check_equal_bitfield(actual.fields[1], ((bitfield_512_t) {0xfffdffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0x1fffffffffffffff, 0x0}))

    //größer als die größte pfn
    pfn = 1361;
    ret = lower_put(&actual, pfn, order);
    check_equal(ret, ERR_ADDRESS)
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t){0xfffffffffffffffe, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))
    check_equal_bitfield(actual.fields[1], ((bitfield_512_t) {0xfffdffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0x1fffffffffffffff, 0x0}))

    free_lower(actual);
    return success;
}


bool is_free_test(){
    bool success = true;

    lower_t actual;
    init_default(&actual, 0, 1360);
    assert(init_lower(&actual, true) == ERR_OK);

    int ret;
    int order = 0;

    pfn_at pfn = 0;
    ret = is_free(&actual, pfn, order);
    check_equal(ret, true);

    pfn = 910;
    ret = is_free(&actual, pfn, order);
    check_equal(ret, true);

    free_lower(actual);

    init_default(&actual, 0, 1360);
    assert(init_lower(&actual, false) == ERR_OK);

    ret = is_free(&actual, pfn, order);
    check_equal(ret, false);

    pfn = 910;
    ret = is_free(&actual, pfn, order);
    check_equal(ret, false);
    assert(lower_put(&actual,513, order) == ERR_OK);
    assert(lower_put(&actual,511, order) == ERR_OK);
    ret = is_free(&actual, 513, order);
    check_equal(ret, true);
    ret = is_free(&actual, 511, order);
    check_equal(ret, true);
    ret = is_free(&actual, 512, order);
    check_equal(ret, false);


    return success;
}


int lower_HP_tests(){
    bool success = true;

    lower_t actual;
    init_default(&actual, 0, FIELDSIZE * 60);
    assert(init_lower(&actual, true) == ERR_OK);

    int64_t pfn1 = lower_get(&actual, 0, HP);
    check( pfn1 >= 0, "");
    uint64_t offset = pfn1 % FIELDSIZE;
    check_uequal(offset, 0ul);
    int64_t pfn2 = lower_get(&actual, 0, HP);
    check( pfn2 >= 0, "");
    offset = pfn2 % FIELDSIZE;
    check_uequal(offset, 0ul);
    check(pfn1 != pfn2, "");
    check_uequal(allocated_frames(&actual), 2ul * FIELDSIZE);
    
    // request a regular frame
    int64_t regular = lower_get(&actual, 0, 0);
    //regular frame cannot be returned as HP
    check_equal(lower_put(&actual, regular, HP), ERR_ADDRESS);


    assert(regular >= 0);
    //this HF must be in another child than the regular frame.
    int64_t pfn3 = lower_get(&actual, 10, HP);
    check(pfn3 >= 0,"");
    offset = pfn3 % FIELDSIZE;
    check_uequal(offset, 0ul);
    check_uequal((uint64_t)pfn3, 3*FIELDSIZE + actual.start_pfn);

    // free regular page und try get this child as complete HP
    assert(lower_put(&actual, regular, 0) == ERR_OK);
    int64_t pfn4 = lower_get(&actual, 0, HP);
    check(pfn4 >= 0,"");
    check(pfn4 == regular, "");


    int ret = lower_put(&actual, pfn2, HP);
    check_equal(ret, ERR_OK);

    //allocate the complete memory with HPs
    for(int i = 3; i < 60; ++i){
        // get allocates only in chunks of 32 children. if there is no free HP in given chung it returns ERR_MEMORY
        int64_t pfn = lower_get(&actual, i < 32? 0 : getAtomicIdx(32*FIELDSIZE), HP);
        check(pfn > 0, "");
    }

    check_uequal_m(allocated_frames(&actual), actual.length, "fully allocated with Huge Frames");

    //reservation at full memory must fail
    int64_t pfn = lower_get(&actual, 0, HP);
    check(pfn == ERR_MEMORY,"");

    // return HP as regular Frame must fail
    check(lower_put(&actual, pfn1, 0) == ERR_ADDRESS, "");

    check(lower_put(&actual, pfn1, HP) == ERR_OK, "");
    check(lower_put(&actual, pfn2, HP) == ERR_OK, "");

    //check if right amout of free reguale frames are present
    check_uequal(actual.length - allocated_frames(&actual), 2ul * FIELDSIZE);

    //new aquired frame should be in same positon as the old no 1
    check(lower_get(&actual, 0, HP) == pfn1, "");


    return success;
}




//runns all tests an returns the number of failed Tests
int lower_tests(int* test_counter, int* fail_counter){

    run_test(init_lower_test);
    run_test(get_test);
    run_test(put_test);
    run_test(is_free_test);
    run_test(lower_HP_tests);
    return 0;
}