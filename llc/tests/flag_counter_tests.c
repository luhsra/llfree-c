#include "flag_counter_tests.h"
#include "check.h"

#include "../flag_counter.c"

#define check_counter(actual,expect)  \
    check_equal(actual.counter, expect.counter);    \
    check_equal(actual.flag, expect.flag);

bool init_flag_counter_test(){
    bool success = true;

    flag_counter_t actual;
    flag_counter_t expect;

    #define test_init(counter,flag) \
        expect = (flag_counter_t) {{{counter,flag}}}; \
        actual = init_flag_counter(counter,flag); \
        check_counter(actual,expect);

    test_init(0x7fff, false);
    test_init(0x7fff, true);
    test_init(0x0, true);
    test_init(0x52a, false);


    return success;
}

bool atomic_flag_set_test(){
    bool success = true;

    flag_counter_t actual = (flag_counter_t) {{{5,0}}};
    flag_counter_t expect = (flag_counter_t) {{{5,true}}};

    int ret = atomic_flag_set(&actual);
    check_equal(ret, 0);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = (flag_counter_t) {{{0x7ffe,true}}};
    expect = (flag_counter_t) {{{0x7ffe,true}}};

    ret = atomic_flag_set(&actual);
    check_equal_m(ret, -2, "must fail if already set");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    return success;
}

bool atomic_flag_reset_test(){
    bool success = true;

    flag_counter_t actual = (flag_counter_t) {{{0x7fff,true}}};
    flag_counter_t expect = (flag_counter_t) {{{0x7fff,false}}};

    int ret = atomic_flag_reset(&actual);
    check_equal(ret, 0);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = (flag_counter_t) {{{5,false}}};
    expect = (flag_counter_t) {{{5,false}}};

    ret = atomic_flag_reset(&actual);
    check_equal_m(ret, -2, "must fail if already reset");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    return success;
}

bool atomic_counter_inc_test(){
    bool success = true;

    flag_counter_t actual = (flag_counter_t) {{{0x7fff,true}}};
    flag_counter_t expect = (flag_counter_t) {{{0x7fff,true}}};

    int ret = atomic_counter_inc(&actual);
    check_equal_m(ret, -1, "out of range");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = (flag_counter_t) {{{5,false}}};
    expect = (flag_counter_t) {{{6,false}}};

    ret = atomic_counter_inc(&actual);
    check_equal(ret, 0);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = (flag_counter_t) {{{0,true}}};
    expect = (flag_counter_t) {{{1,true}}};

    ret = atomic_counter_inc(&actual);
    check_equal(ret, 0);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = (flag_counter_t) {{{0x3fff,true}}};
    expect = (flag_counter_t) {{{0x4000,true}}};

    ret = atomic_counter_inc(&actual);
    check_equal(ret, 0);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    return success;
}

bool atomic_counter_dec_test(){
    bool success = true;

    flag_counter_t actual = (flag_counter_t) {{{0,true}}};
    flag_counter_t expect = (flag_counter_t) {{{0,true}}};

    int ret = atomic_counter_dec(&actual);
    check_equal_m(ret, -1, "out of range");
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = (flag_counter_t) {{{9,false}}};
    expect = (flag_counter_t) {{{8,false}}};

    ret = atomic_counter_dec(&actual);
    check_equal(ret, 0);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);

    actual = (flag_counter_t) {{{0x7fff,true}}};
    expect = (flag_counter_t) {{{0x7ffe,true}}};

    ret = atomic_counter_dec(&actual);
    check_equal(ret, 0);
    check_equal(actual.counter, expect.counter);
    check_equal(actual.flag, expect.flag);


    return success;
}


int flag_counter_tests(int* test_counter, int* fail_counter){
    run_test(init_flag_counter_test());
    run_test(atomic_flag_set_test());
    run_test(atomic_flag_reset_test());
    run_test(atomic_counter_inc_test());
    run_test(atomic_counter_dec_test());

    return 0;
}