#include "../lower.h"
#include "../bitfield.h"
#include "../lower.h"
#include "check.h"
#include <assert.h>
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


bool init_test(){
    bool success = true;

    int pfn_start = 0;
    int frames = 512;
    lower_t actual;
    init_default(&actual, pfn_start, frames);
    int ret = init_lower(&actual, pfn_start, frames, false);

    check_child_number(1ul);
    bitfield_is_free(actual.fields[0])
    check_uequal(allocated_frames(&actual),0ul)
    free_lower(actual)

    pfn_start = 0;
    frames = 511;
    init_default(&actual, pfn_start, frames);
    ret = init_lower(&actual, pfn_start, frames, false);
    check_child_number(1ul);
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t) {0,0,0,0,0,0,0,0x8000000000000000}))
    check_uequal(allocated_frames(&actual),0ul)
    free_lower(actual)

    pfn_start = 0;
    frames = 632;
    init_default(&actual, pfn_start, frames);
    ret = init_lower(&actual, pfn_start, frames, true);
    check_child_number(2ul);
    bitfield_is_blocked_n(actual.fields,2)
    check_uequal(allocated_frames(&actual),632ul)
    free_lower(actual)

    pfn_start = 0;
    frames = 968;
    init_default(&actual, pfn_start, frames);
    ret = init_lower(&actual, pfn_start, frames, false);
    check_child_number(2ul);
    bitfield_is_free(actual.fields[0])
    check_equal_bitfield(actual.fields[1], ((bitfield_512_t) {0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xffffffffffffff00}))
    check_uequal(allocated_frames(&actual),0ul)
    free_lower(actual)


    pfn_start = 0;
    frames = 685161;
    init_default(&actual, pfn_start, frames);
    ret = init_lower(&actual, pfn_start, frames, false);
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
    assert(init_lower(&actual, 0, 1360, false) == ERR_OK);

    pfn_at pfn;
    int ret;
    int order = 0;

    ret = lower_get(&actual,0,order,&pfn);
    check_equal(ret, ERR_OK);
    check_uequal(pfn, 0ul);
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t) {0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}))

    ret = lower_get(&actual,0,order,&pfn);
    check_equal(ret, ERR_OK);
    check_uequal(pfn, 1ul);
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t) {0x3, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}))


    ret = lower_get(&actual,320,order,&pfn);
    check_equal(ret, ERR_OK);
    check_uequal(pfn, 2ul);
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t) {0x7, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}))

    for(int i = 0; i < 954; i++){
        ret = lower_get(&actual,0,order,&pfn);
        check_equal(ret, ERR_OK);
        check_uequal(pfn, (i + 3ul));
    }
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t){0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))
    check_equal_bitfield(actual.fields[1], ((bitfield_512_t) {0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0x1fffffffffffffff, 0x0}))


    free_lower(actual)
    init_default(&actual, 0, 2);
    assert(init_lower(&actual, 0, 2, false) == ERR_OK);

    ret = lower_get(&actual,0,order,&pfn);
    check_equal(ret, ERR_OK);
    check_uequal(pfn, 0ul);
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t) {0xfffffffffffffffd, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))

    ret = lower_get(&actual,0,order,&pfn);
    check_equal(ret, ERR_OK);
    check_uequal(pfn, 1ul);
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t) {0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))

    ret = lower_get(&actual,0,order,&pfn);
    check_equal(ret, ERR_MEMORY);
    check_equal_bitfield(actual.fields[0], ((bitfield_512_t) {0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))

    return success;
}


bool put_test(){
    bool success = true;

    lower_t actual;
    init_default(&actual, 0, 1360);
    assert(init_lower(&actual, 0, 1360, false) == ERR_OK);

    pfn_at pfn;
    int ret;
    int order = 0;

    for(int i = 0; i < 957; i++){
        ret = lower_get(&actual,0,order, &pfn);
        assert(ret == ERR_OK);
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
    assert(init_lower(&actual, 0, 1360, false) == ERR_OK);

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
    assert(init_lower(&actual, 0, 1360, true) == ERR_OK);

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







//runns all tests an returns the number of failed Tests
int lower_tests(int* test_counter, int* fail_counter){

    run_test(init_test());
    run_test(get_test());
    run_test(put_test());
    run_test(is_free_test());

    return 0;
}