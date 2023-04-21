#include "flag_counter_tests.h"
#include "check.h"

#include "../flag_counter.h"
#include "../bitfield.h"

#define check_counter(actual,expect)  \
    check_equal(actual.counter, expect.counter);    \
    check_equal(actual.flag, expect.flag);



bool reserve_HP_test(){
    bool success = true;

    flag_counter_t actual = init_flag_counter(512,0);
    flag_counter_t expect = init_flag_counter(0,true);

    int ret = reserve_HP(&actual);
    check_equal(ret, ERR_OK);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = init_flag_counter(0,true);
    expect = init_flag_counter(0,true);

    ret = reserve_HP(&actual);
    check_equal_m(ret, ERR_MEMORY, "must fail if already set");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);


    actual = init_flag_counter(320,false);
    expect = init_flag_counter(320,false);

    ret = reserve_HP(&actual);
    check_equal_m(ret, ERR_MEMORY, "must fail if some frame are allocated");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);
    return success;
}

bool free_HP_test(){
    bool success = true;

    flag_counter_t actual = init_flag_counter(0,true);
    flag_counter_t expect = init_flag_counter(FIELDSIZE,false);

    int ret = free_HP(&actual);
    check_equal(ret, ERR_OK);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = init_flag_counter(0,false);
    expect = init_flag_counter(0,false);

    ret = free_HP(&actual);
    check_equal_m(ret, ERR_ADDRESS, "must fail if already reset");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);


    actual = init_flag_counter(320,true);
    expect = init_flag_counter(320,true);

    ret = free_HP(&actual);
    check_equal_m(ret, ERR_ADDRESS, "should not be possible to have a flag with a counter > 0");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    return success;
}

bool atomic_counter_inc_test(){
    bool success = true;

    flag_counter_t actual = init_flag_counter(0x7fff,true);
    flag_counter_t expect = init_flag_counter(0x7fff,true);

    int ret = atomic_counter_inc(&actual);
    check_equal_m(ret, ERR_MEMORY, "out of range");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = init_flag_counter(5,false);
    expect = init_flag_counter(6,false);

    ret = atomic_counter_inc(&actual);
    check_equal(ret, ERR_OK);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = init_flag_counter(0,true);
    expect = init_flag_counter(1,true);

    ret = atomic_counter_inc(&actual);
    check_equal(ret, ERR_OK);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = init_flag_counter(0x3fff,true);
    expect = init_flag_counter(0x4000,true);

    ret = atomic_counter_inc(&actual);
    check_equal(ret, ERR_OK);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    return success;
}

bool atomic_counter_dec_test(){
    bool success = true;

    flag_counter_t actual = init_flag_counter(0,true);
    flag_counter_t expect = init_flag_counter(0,true);

    int ret = atomic_counter_dec(&actual);
    check_equal_m(ret, ERR_MEMORY, "out of range");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = init_flag_counter(9,false);
    expect = init_flag_counter(8,false);

    ret = atomic_counter_dec(&actual);
    check_equal(ret, 0);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = init_flag_counter(0x7fff,true);
    expect = init_flag_counter(0x7ffe,true);

    ret = atomic_counter_dec(&actual);
    check_equal(ret, 0);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);


    return success;
}


int flag_counter_tests(int* test_counter, int* fail_counter){
    run_test(reserve_HP_test());
    run_test(free_HP_test());
    run_test(atomic_counter_inc_test());
    run_test(atomic_counter_dec_test());

    return 0;
}