#include "child_tests.h"
#include "check.h"

#include "../child.h"
#include "../bitfield.h"

#define check_counter(actual,expect)  \
    check_equal(actual.counter, expect.counter);    \
    check_equal(actual.flag, expect.flag);



bool reserve_HP_test(){
    bool success = true;

    child_t actual = init_child(512,false);
    child_t expect = init_child(0,true);

    int ret = reserve_HP(&actual);
    check_equal(ret, ERR_OK);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = init_child(0,true);
    expect = init_child(0,true);

    ret = reserve_HP(&actual);
    check_equal_m(ret, ERR_MEMORY, "must fail if already set");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);


    actual = init_child(320,false);
    expect = init_child(320,false);

    ret = reserve_HP(&actual);
    check_equal_m(ret, ERR_MEMORY, "must fail if some frame are allocated");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);
    return success;
}

bool free_HP_test(){
    bool success = true;

    child_t actual = init_child(0,true);
    child_t expect = init_child(FIELDSIZE,false);

    int ret = free_HP(&actual);
    check_equal(ret, ERR_OK);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = init_child(0,false);
    expect = init_child(0,false);

    ret = free_HP(&actual);
    check_equal_m(ret, ERR_ADDRESS, "must fail if already reset");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);


    actual = init_child(320,true);
    expect = init_child(320,true);

    ret = free_HP(&actual);
    check_equal_m(ret, ERR_ADDRESS, "should not be possible to have a flag with a counter > 0");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    return success;
}

bool child_counter_inc_test(){
    bool success = true;

    child_t actual = init_child(0x200,true);
    child_t expect = init_child(0x200,true);

    int ret = child_counter_inc(&actual);
    check_equal_m(ret, ERR_ADDRESS, "out of range");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = init_child(5,false);
    expect = init_child(6,false);

    ret = child_counter_inc(&actual);
    check_equal(ret, ERR_OK);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = init_child(0,true);
    expect = init_child(0,true);

    ret = child_counter_inc(&actual);
    check_equal(ret, ERR_ADDRESS);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = init_child(0x01ff,false);
    expect = init_child(0x0200,false);

    ret = child_counter_inc(&actual);
    check_equal(ret, ERR_OK);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    return success;
}

bool child_counter_dec_test(){
    bool success = true;

    child_t actual = init_child(0,false);
    child_t expect = init_child(0,false);

    int ret = child_counter_dec(&actual);
    check_equal_m(ret, ERR_MEMORY, "out of range");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = init_child(9,false);
    expect = init_child(8,false);

    ret = child_counter_dec(&actual);
    check_equal(ret, ERR_OK);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = init_child(FIELDSIZE,false);
    expect = init_child(FIELDSIZE-1,false);

    ret = child_counter_dec(&actual);
    check_equal(ret, ERR_OK);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = init_child(320,true);
    expect = init_child(320,true);

    ret = child_counter_dec(&actual);
    check_equal(ret, ERR_MEMORY);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    return success;
}


int child_tests(int* test_counter, int* fail_counter){
    run_test(reserve_HP_test);
    run_test(free_HP_test);
    run_test(child_counter_inc_test);
    run_test(child_counter_dec_test);

    return 0;
}