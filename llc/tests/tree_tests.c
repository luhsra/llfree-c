#include "tree_tests.h"
#include "check.h"
#include "../tree.h"
#include "../enum.h"


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


int tree_tests(int* test_counter, int* fail_counter){
    run_test(init_tree_test)
    run_test(reserve_test)
    run_test(unreserve_test);


    return 0;
}