#include <stdio.h>
#include <stdbool.h>

extern bool sucssess;

#define check(x,msg)    \
    if(!(x)) {          \
        printf("\tFILE %s: LINE %d:\n\tCheck " #x " failed: %s\n",__FILE__, __LINE__, msg); \
        sucssess = false;   \
    }

#define check_equal_m(actual,expected, msg)   \
    if(actual != expected) {    \
        printf("\tFILE %s: LINE %d: Check " #actual " == " #expected " failed: %s\n\texpected: %d, actual value: %d\n",__FILE__, __LINE__, msg, expected, actual); \
        sucssess = false;   \
    }
#define check_equal(actual, expected) check_equal_m(actual, expected, "")