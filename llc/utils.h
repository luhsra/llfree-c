#pragma once
#include <bits/stdint-uintn.h>
#include <stdint.h>
#include <stddef.h>
#include "stdbool.h"

//TODO function descriptions

size_t div_ceil(uint64_t a, int b);


//cas breaks if it returns false; you can change desire based on old for next step
bool update16_default(uint16_t* current, uint16_t* desire);
bool update64_default(uint64_t* current, uint64_t* desire);


#define FIRST(A, ...) A
#define cas(_1, ...) _Generic((FIRST(__VA_ARGS__)),                                     \
                            bool(*)(uint16_t*, uint16_t*): cas16_update,                \
                            bool(*)(uint64_t*, uint64_t*): cas64_update,                \
                            uint16_t*:                  cas16_complete,                 \
                            uint64_t*:                  cas64_complete                  \
                            )(_1, __VA_ARGS__)

int cas16_complete(_Atomic(uint16_t)* obj, uint16_t* expect, bool (*update)(uint16_t* expect, uint16_t* desire));

int cas16_update(_Atomic(uint16_t)* obj, bool (*update)(uint16_t* expect, uint16_t* desire));

int cas64_complete(_Atomic(uint64_t)* obj, uint64_t* expect, bool (*update)(uint64_t* expect, uint64_t* desire));

int cas64_update(_Atomic(uint64_t)* obj, bool (*update)(uint64_t* expect, uint64_t* desire));
