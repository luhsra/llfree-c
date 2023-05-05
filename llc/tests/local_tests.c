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
    check_equal(actual.reserved.free_counter, 0);
    check_equal(actual.reserved.reservation_in_progress, false);
    check_equal(actual.reserved.has_reserved_tree,false);

    return success;
}



int local_tests(int* test_counter, int* fail_counter){
    run_test(init_local_test);


    return 0;
}