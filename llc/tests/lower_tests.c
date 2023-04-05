#include "../lower.c"
#include "bitfield.h"
#include "lower.h"
#include "tests/check.h"

#define check_child_number(expect)  \
    check_uequal_m(num_of_childs(actual), expect, "schould have exactly 1 Child per 512 Frames");

#define bitfield_is_free(actual)    \
    check_equal_bitfield(actual, ((bitfield_512_t) {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}))
#define bitfield_is_blocked(actual) \
    check_equal_bitfield(actual, ((bitfield_512_t) {0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}))

#define bitfield_is_free_n(actual, n)    \
    for(size_t i = 0; i < n; i++){  \
        bitfield_is_free(&actual[i])    \
    }

#define free_lower(lower)   \
    free(lower ->fields);   \
    free(lower->childs);    \
    free(lower); 


bool init_default_test(){
    bool success = true;


    lower_t* actual = init_default(0, 512, false);
    check_child_number(1ul);
    bitfield_is_free(actual->fields[0])
    free_lower(actual)

    actual = init_default(0, 512, true);
    check_child_number(1ul);
    bitfield_is_blocked(actual->fields[0])
    free_lower(actual)


    return success;
}














//runns all tests an returns the number of failed Tests
int lower_tests(int* test_counter, int* fail_counter){

    run_test(init_default_test());

    return 0;
}