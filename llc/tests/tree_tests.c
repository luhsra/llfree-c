#include "tree_tests.h"
#include "check.h"
#include "../tree.h"
#include "../enum.h"
#include <stdbool.h>


#define equal_trees(actual,expect) \
    check_equal(actual.counter, expect.counter);   \
    check_equal(actual.flag, expect.flag);

bool init_tree_test(){
    int success = true;

    int counter = 498;
    bool flag = false;

    tree_t actual = init_tree(counter, flag);
    check_equal(actual.counter, counter);
    check_equal(actual.flag, flag);

    counter = 0x3fff; //maximum value
    flag = false;
    actual = init_tree(counter, flag);
    check_equal(actual.counter, counter);
    check_equal(actual.flag, flag);

    counter = 0; //minimum value
    flag = true;    //check if flag is set
    actual = init_tree(counter, flag);
    check_equal(actual.counter, counter);
    check_equal(actual.flag, flag);

    return success;
}

bool reserve_test(){
    int success = true;

    int counter = 7645;
    tree_t actual = init_tree(counter, false);
    tree_t expect = init_tree(0,true);

    int ret = reserve_tree(&actual);
    check_equal(ret, counter);
    equal_trees(actual, expect)

    // chek min counter value
    counter = 0;
    actual = init_tree(counter, false);
    expect = init_tree(0,true);

    ret = reserve_tree(&actual);
    check_equal(ret, counter);
    equal_trees(actual, expect)

    // if already reserved
    counter = 456;
    actual = init_tree(counter, true);
    expect = actual; // no change expected
    ret = reserve_tree(&actual);
    check_equal(ret, ERR_ADDRESS);
    equal_trees(actual, expect)

    //max counter value
    counter = 0x3fff;
    actual = init_tree(counter, false);
    expect = init_tree(0,true);

    ret = reserve_tree(&actual);
    check_equal(ret, counter);
    equal_trees(actual, expect)

    return success;
}

bool unreserve_test(){
    int success = true;

    int counter;
    int frees;
    tree_t actual;
    tree_t expect;
    int ret;

    counter = 0;
    frees = 9873;
    actual = init_tree(counter, true);
    expect = init_tree(counter + frees, false);

    ret = unreserve_tree(&actual, frees);
    check_equal(ret, ERR_OK);
    equal_trees(actual, expect)

    counter = 4532;
    frees = 9873;
    actual = init_tree(counter, true);
    expect = init_tree(counter + frees, false);

    ret = unreserve_tree(&actual, frees);
    check_equal(ret, ERR_OK);
    equal_trees(actual, expect)

    return success;
}

bool tree_status_test(){
    bool success = true;

    tree_t actual;
    tree_t expect;
    int ret;

    actual = init_tree(1, false);
    expect = actual; // no change

    ret = tree_status(&actual);
    check_equal(ret, ALLOCATED);
    equal_trees(actual, expect)


    actual = init_tree(1, false);
    expect = actual; // no change

    ret = tree_status(&actual);
    check_equal(ret, ALLOCATED);
    equal_trees(actual, expect)


    return success;
}


bool tree_inc_test(){
    bool success = true;

    tree_t actual;
    tree_t expect;
    size_t order;
    int counter;
    int ret;

    order = 0;
    counter = 0;
    actual = init_tree(counter, false);
    expect = init_tree(counter + (1 << order), false);

    ret = tree_counter_inc(&actual, order);
    check_equal(ret, ERR_OK)
    equal_trees(actual, expect)

    order = 0;
    counter = 16383;    //max counter for success
    actual = init_tree(counter, false);
    expect = init_tree(counter + (1 << order), false);
    ret = tree_counter_inc(&actual, order);

    check_equal(ret, ERR_OK)
    equal_trees(actual, expect)


    order = 0;
    counter = 3456;
    actual = init_tree(counter, true);  //should be no differende if flag is true
    expect = init_tree(counter + (1 << order), true);

    ret = tree_counter_inc(&actual, order);
    check_equal(ret, ERR_OK)
    equal_trees(actual, expect)


    order = 9;  //HP
    counter = 3456;
    actual = init_tree(counter, true);
    expect = init_tree(counter + (1 << order), true);

    ret = tree_counter_inc(&actual, order);
    check_equal(ret, ERR_OK)
    equal_trees(actual, expect)


    order = 9;  //HP
    counter = (1 << 14) - (1 << 9); // max counter
    actual = init_tree(counter, true);
    expect = init_tree(counter + (1 << order), true);

    ret = tree_counter_inc(&actual, order);
    check_equal(ret, ERR_OK)
    equal_trees(actual, expect)

    return success;
}

bool tree_dec_test(){
    bool success = true;

    tree_t actual;
    tree_t expect;
    size_t order;
    int counter;
    int ret;

    order = 0;
    counter = 1 << 14;
    actual = init_tree(counter, false);
    expect = init_tree(counter - (1 << order), false);

    ret = tree_counter_dec(&actual, order);
    check_equal(ret, ERR_OK)
    equal_trees(actual, expect)

    order = 0;
    counter = 1;    //min counter for success
    actual = init_tree(counter, false);
    expect = init_tree(counter - (1 << order), false);
    ret = tree_counter_dec(&actual, order);

    check_equal(ret, ERR_OK)
    equal_trees(actual, expect)


    order = 0;
    counter = 3456;
    actual = init_tree(counter, true);  //should be no differende if flag is true
    expect = init_tree(counter - (1 << order), true);

    ret = tree_counter_dec(&actual, order);
    check_equal(ret, ERR_OK)
    equal_trees(actual, expect)


    order = 9;  //HP
    counter = 13370;
    actual = init_tree(counter, true);
    expect = init_tree(counter - (1 << order), true);

    ret = tree_counter_dec(&actual, order);
    check_equal(ret, ERR_OK)
    equal_trees(actual, expect)


    order = 9;  //HP
    counter = (1 << 9); // min counter
    actual = init_tree(counter, true);
    expect = init_tree(counter - (1 << order), true);

    ret = tree_counter_dec(&actual, order);
    check_equal(ret, ERR_OK)
    equal_trees(actual, expect)


    return success;
}


int tree_tests(int* test_counter, int* fail_counter){
    run_test(init_tree_test)
    run_test(reserve_test)
    run_test(unreserve_test);
    run_test(tree_status_test)
    run_test(tree_inc_test)
    run_test(tree_dec_test)

    return 0;
}