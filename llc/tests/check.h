#pragma once

#include <stdio.h>
#include <stdbool.h>


#define run_test(test_func) \
    (*test_counter)++;  \
    printf(#test_func ":\n");    \
    if(!test_func()) (*fail_counter)++;   \
    else printf("\tsuccess\n"); \


#define check(x,msg)    \
    if(!(x)) {          \
        printf("\tFILE %s:%d:\n\tCheck " #x " failed: %s\n",__FILE__, __LINE__, msg); \
        success = false;   \
    }

#define check_equal_m(actual,expected, msg)   \
    if(actual != expected) {    \
        printf("\tFILE %s:%d: Check " #actual " == " #expected " failed: %s\n\texpected: %d, actual value: %d\n",__FILE__, __LINE__, msg, expected, actual); \
        success = false;   \
    }
#define check_equal(actual, expected) check_equal_m(actual, expected, "")

#define check_uequal_m(actual,expected, msg)   \
    if(actual != expected) {    \
        printf("\tFILE %s:%d: Check " #actual " == " #expected " failed: %s\n\texpected: %lu, actual value: %lu\n",__FILE__, __LINE__, msg, expected, actual); \
        success = false;   \
    }
#define check_uequal(actual, expected) check_uequal_m(actual, expected, "")


#define check_equal_bitfield_m(actual, expected,msg) \
    if(!equals(&actual, &expected)) { \
        printf("\tFILE %s:%d: Check equal bitfields failed: %s\n\tactual:\n",__FILE__, __LINE__, msg); \
        print_field(&actual);   \
        printf("\texpected:\n");    \
        print_field(&expected);   \
        success = false;   \
    }
#define check_equal_bitfield(actual, expected) check_equal_bitfield_m(actual, expected,"")
