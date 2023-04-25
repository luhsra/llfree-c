#include "local_tests.h"
#include "../local.h"
#include "check.h"


//TODO all function tests
bool init_local_test(){
    bool success = true;

    local_t actual;
    init_local(&actual);
    check_equal(actual.last_free.free_counter, 0);
    check_uequal(actual.last_free.last_free_idx, 0ul);
    check_equal(actual.reserved.fcounter, 0);
    check_equal(actual.reserved.in_reservation, false);
    check_uequal(actual.reserved.preferred_index, MAX_TREE_INDEX);

    return success;
}



int local_tests(int* test_counter, int* fail_counter){
    run_test(init_local_test);


    return 0;
}