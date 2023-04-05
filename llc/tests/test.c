#include <stdio.h>


#include "bitfield_tests.h"
#include "flag_counter_tests.h"
#include "lower_tests.h"



int main(){
    int test_counter = 0;
    int fail_counter = 0;
    printf("Running Bitfield Tests:\n");
    bitfield_tests(&test_counter, &fail_counter);
    printf("---------------------------------------\n");

    printf("Running Flag-counter Tests:\n");
    flag_counter_tests(&test_counter, &fail_counter);
    printf("---------------------------------------\n");

    printf("Running lower allocator Tests:\n");
    lower_tests(&test_counter, &fail_counter);
    printf("---------------------------------------\n");
    
    
    
    
    
    if(fail_counter == 0)
        printf("----------------SUCCESS----------------\n");
    else printf("----------------FAILED----------------\n");
    printf("---------------------------------------\n");
    printf("Failed %d out of %d tests.\n",fail_counter, test_counter);
}