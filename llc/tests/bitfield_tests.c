#include "bitfield_tests.h"
#include "../bitfield.c"

bool get_pos_test(){
    return false;
}


int bitfield_tests(int* test_counter, int* fail_counter){
    (*test_counter)++;
    if(!get_pos_test()) (*fail_counter)++;

    return 0;
}
