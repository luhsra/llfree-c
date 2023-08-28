#include "utils.h"
#include "assert.h"
#include "enum.h"
#include "child.h"
#include <stdatomic.h>
#include <stdint.h>


size_t div_ceil(uint64_t a, int b){
    return a % b ? a / b + 1 : a / b;
}
