#include "bitfield_tests.h"
#include "check.h"

#include "../bitfield.c"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

bool init_field_test(){
    bool success = true;

    bitfield_512_t expect = {{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
    bitfield_512_t actual;
    actual = init_field(0, false);
    check_equal_bitfield_m(actual, expect, "full init");

    expect = (bitfield_512_t) {{0xfffffffffffffffe, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}};
    actual = init_field(1, false);
    check_equal_bitfield_m(actual, expect, "init with no free frames");


    expect = (bitfield_512_t) {{0x0, 0x0, 0x0, 0xffc0000000000000, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}};
    actual = init_field(246, false);
    check_equal_bitfield_m(actual, expect, "init with partial frames");


    //Initial all allocated tests

    expect = (bitfield_512_t) {{0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}};
    actual = init_field(312, true);
    check_equal_bitfield_m(actual, expect, "all allocated");

    expect = (bitfield_512_t) {{0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}};
    actual = init_field(0, true);
    check_equal_bitfield_m(actual, expect, "all allocated");


    actual = init_field(1, true);
    check_equal_bitfield_m(actual, expect, "all allocated");


    return success;
}

bool find_unset_test(){
    bool success = true;

    bitfield_512_t actual = (bitfield_512_t) {{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
    bitfield_512_t expected = actual;
    pos_t pos;

    int ret = find_unset(&actual, &pos);
    check_equal(ret, 0);
    check_equal(pos.row_number, 0);
    check_equal(pos.bit_number, 0);
    check_equal_bitfield_m(actual, expected, "field should not be changed");

    actual = (bitfield_512_t) {{0x1,0x0,0x0,0x1,0x0,0x0,0x0,0x0}};
    expected = actual;
    ret = find_unset(&actual, &pos);
    check_equal(ret, 0);
    check_equal(pos.row_number, 0);
    check_equal(pos.bit_number, 1);
    check_equal_bitfield_m(actual, expected, "field should not be changed");

    actual = (bitfield_512_t) {{0x1fffffffff,0x0,0x0,0x1,0x0,0x0,0x0,0xffffffffffffffff}};
    expected = actual;
    ret = find_unset(&actual, &pos);
    check_equal(ret, 0);
    check_equal(pos.row_number, 0);
    check_equal(pos.bit_number, 37);
    check_equal_bitfield_m(actual, expected, "field should not be changed");


    actual = (bitfield_512_t) {{0xffffffffffffffff,0xffffffffffffffff,0x0,0x1,0x0,0xffffffffffffffff,0x0,0x0}};
    expected = actual;
    ret = find_unset(&actual, &pos);
    check_equal(ret, 0);
    check_equal(pos.row_number, 2);
    check_equal(pos.bit_number, 0);
    check_equal_bitfield_m(actual, expected, "field should not be changed");


    actual = (bitfield_512_t) {{0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0x7fffffffffffffff,0x0,0x0,0xffffffffffffffff,0x0}};
    expected = actual;
    ret = find_unset(&actual, &pos);
    check_equal(ret, 0);
    check_equal(pos.row_number, 3);
    check_equal(pos.bit_number, 63);
    check_equal_bitfield_m(actual, expected, "field should not be changed");

    actual = (bitfield_512_t) {{0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0x7fffffffffffffff}};
    expected = actual;
    ret = find_unset(&actual, &pos);
    check_equal(ret, 0);
    check_equal(pos.row_number, 7);
    check_equal(pos.bit_number, 63);
    check_equal_bitfield_m(actual, expected, "field should not be changed");

    actual = (bitfield_512_t) {{0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff}};
    expected = actual;
    ret = find_unset(&actual, &pos);
    check_equal_m(ret, ERR_MEMORY, "should be no space available");
    check_equal_bitfield_m(actual, expected, "field should not be changed");
    


    return success;
}

bool set_Bit_test(){
    bool success = true;

    bitfield_512_t actual =   (bitfield_512_t) {{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
    bitfield_512_t expected = (bitfield_512_t) {{0x1,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};

    int ret = set_Bit(&actual);
    check_equal(ret, 0);
    check_equal_bitfield_m(actual, expected, "first bit must be set");


    actual = (bitfield_512_t)   {{0x1,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
    expected = (bitfield_512_t) {{0x3,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};

    ret = set_Bit(&actual);
    check_equal(ret, 1);
    check_equal_bitfield(actual, expected);

    actual = (bitfield_512_t)   {{0xffffffffffffffff,0xf,0x0,0x0,0x0,0x0,0x0,0x0}};
    expected = (bitfield_512_t) {{0xffffffffffffffff,0x1f,0x0,0x0,0x0,0x0,0x0,0x0}};

    ret = set_Bit(&actual);
    check_equal_m(ret, 68, "call should be a success");
    check_equal_bitfield(actual, expected);


    actual = (bitfield_512_t)   {{0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xfabdeadbeeffffff,0x0,0xdeadbeefdeadbeef,0x0,0x8000000000000000}};
    expected = (bitfield_512_t) {{0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xfabdeadbefffffff,0x0,0xdeadbeefdeadbeef,0x0,0x8000000000000000}};

    ret = set_Bit(&actual);
    check_equal_m(ret, 216, "call should be a success");
    check_equal_bitfield_m(actual, expected, "row 3 bit 24 -> e to f");


    actual = (bitfield_512_t)   {{0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}};
    expected = (bitfield_512_t) {{0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}};

    ret = set_Bit(&actual);
    check_equal_m(ret, ERR_MEMORY, "call should fail");
    check_equal_bitfield_m(actual, expected, "no change");

    return success;
}

bool reset_Bit_test(){
    bool success = true;

    bitfield_512_t actual = (bitfield_512_t) {{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
    bitfield_512_t expect = actual;
    uint64_t pos = 0;

    int ret = reset_Bit(&actual, pos);
    check_equal_bitfield_m(actual, expect, "no change if original Bit was already 0");
    check_equal(ret, -1);


    actual = (bitfield_512_t) {{0x1,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
    expect = (bitfield_512_t) {{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
    pos = 0;

    ret = reset_Bit(&actual, pos);
    check_equal(ret, 0);
    check_equal_bitfield_m(actual, expect, "first one should be set to 0");

    actual = (bitfield_512_t) {{0x1,0xfacb8ffabf000000,0xbadc007cd,0x0,0x0,0x0,0x0,0xfffffacbfe975530}};
    expect = (bitfield_512_t) {{0x1,0xfacb8ffabf000000,0xbadc007cd,0x0,0x0,0x0,0x0,0x7ffffacbfe975530}};
    pos = 7ul * 64 + 63;

    ret = reset_Bit(&actual, pos);
    check_equal(ret, 0);
    check_equal_bitfield_m(actual, expect, "last bit should be set to 0");

    actual = (bitfield_512_t) {{0x1,0xfacb8ffabf000000,0xbadc007cd,0x0,0x0,0x0,0x0,0x7ffffacbfe975530}};
    expect = (bitfield_512_t) {{0x1,0xfacb8ffabf000000,0xaadc007cd,0x0,0x0,0x0,0x0,0x7ffffacbfe975530}};
    pos = 2ul * 64 + 32;


    ret = reset_Bit(&actual, pos);
    check_equal(ret, 0);
    check_equal_bitfield_m(actual, expect, "row 2 bit 31 -> b to a");

    actual = (bitfield_512_t) {{0x1,0xfacb8ffabf000000,0xb2dc007cd,0x0,0x0,0x0,0x0,0x7ffffacbfe975530}};
    expect = (bitfield_512_t) {{0x1,0xfacb8ffabf000000,0xb2dc007cd,0x0,0x0,0x0,0x0,0x7ffffacbfe975530}};
    pos = 4ul * 64 + 62;

    ret = reset_Bit(&actual, pos);
    check_equal(ret, -1);
    check_equal_bitfield_m(actual, expect, "no change");

    return success;
}


bool count_Set_Bits_test(){
    bool success = true;

    bitfield_512_t actual = (bitfield_512_t) {{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
    bitfield_512_t expect = (bitfield_512_t) {{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};

    
    int ret = count_Set_Bits(&actual);
    check_equal_m(ret, 0, "no bits set");
    check_equal_bitfield_m(actual, expect, "no change!");


    actual = (bitfield_512_t) {{0x1,0x0,0x0,0x0,0x0,0x0,0x0,0x8000000000000000}};
    expect = (bitfield_512_t) {{0x1,0x0,0x0,0x0,0x0,0x0,0x0,0x8000000000000000}};
    
    ret = count_Set_Bits(&actual);
    check_equal_m(ret, 2, "first and last bit set");
    check_equal_bitfield_m(actual, expect, "no change!");


    actual = (bitfield_512_t) {{0x1,0x0,0x0,0xfffdeadbeef4531,0x0,0x0,0x0,0x80000000f0000000}};
    expect = (bitfield_512_t) {{0x1,0x0,0x0,0xfffdeadbeef4531,0x0,0x0,0x0,0x80000000f0000000}};

    
    ret = count_Set_Bits(&actual);
    check_equal_m(ret, 48, "some bits set");
    check_equal_bitfield_m(actual, expect, "no change!");



    actual = (bitfield_512_t) {{0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}};
    expect = (bitfield_512_t) {{0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}};

    
    ret = count_Set_Bits(&actual);
    check_equal_m(ret, 512, "all bits set");
    check_equal_bitfield_m(actual, expect, "no change!");


    return success;
}


int bitfield_tests(int* test_counter, int* fail_counter){
    
    run_test(init_field_test());
    run_test(find_unset_test());
    run_test(set_Bit_test());
    run_test(reset_Bit_test());
    run_test(count_Set_Bits_test());

    return 0;
}
