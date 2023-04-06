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
        bitfield_is_free(actual[i])    \
    }
#define bitfield_is_blocked_n(actual, n)    \
    for(size_t i = 0; i < n; i++){  \
        bitfield_is_blocked(actual[i])    \
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
    check_uequal(allocated_frames(actual),0ul)
    free_lower(actual)

    actual = init_default(0, 511, true);
    check_child_number(1ul);
    bitfield_is_blocked(actual->fields[0])
    check_uequal(allocated_frames(actual),511ul)
    free_lower(actual)

    actual = init_default(0, 632,true);
    check_child_number(2ul);
    bitfield_is_blocked_n(actual->fields,2)
    check_uequal(allocated_frames(actual),632ul)
    free_lower(actual)

    actual = init_default(0, 968,false);
    check_child_number(2ul);
    bitfield_is_free(actual->fields[0])
    check_equal_bitfield(actual->fields[1], ((bitfield_512_t) {0x0,0x0,0x0,0x0,0x0,0x0,0x0,0xffffffffffffff00}))
    check_uequal(allocated_frames(actual),0ul)
    free_lower(actual)


    actual = init_default(0, 685161,false);
    check_child_number(1339ul);
    bitfield_is_free_n(actual->fields, 1338)
    check_equal_bitfield(actual->fields[1338], ((bitfield_512_t) {0x0,0xfffffe0000000000,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff}))
    check_uequal(allocated_frames(actual),0ul)
    free_lower(actual)


    return success;
}














//runns all tests an returns the number of failed Tests
int lower_tests(int* test_counter, int* fail_counter){

    run_test(init_default_test());

    return 0;
}