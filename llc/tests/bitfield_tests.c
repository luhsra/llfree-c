#include "bitfield_tests.h"
#include "check.h"

#include "../bitfield.c"

bool init_field_test(){
    bool sucssess = true;
    bitfield_512_t f = {{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
    bitfield_512_t field0 = {{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
    bitfield_512_t field1 = {{0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}};

    int ret = init_field(&field1, 512);
    check(equals(&field1, &f), "full init");
    check_equal(ret, 0);

    ret = init_field(&field0, 512);
    check(equals(&field0, &f), "Full init");
    check_equal(ret, 0);


    field0 = (bitfield_512_t) {{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
    field1 = (bitfield_512_t) {{0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff}};

    ret = init_field(&field0,0);
    check(equals(&field0, &field1), "init with no free frames");
    check_equal(ret, 0);


    field0 = (bitfield_512_t) {{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
    f = (bitfield_512_t) {{0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff, 0x003fffffffffffff, 0x0, 0x0, 0x0, 0x0}};

    ret = init_field(&field0,246);
    check(equals(&field0, &f), "init with partial frames");
    check_equal(ret, 0);


    ret = init_field(&field1,246);
    check(equals(&field1, &f), "init with partial frames");
    check_equal(ret, 0);

    return sucssess;
}

bool find_unset_test(){
    bool sucssess = true;

    bitfield_512_t field0 = (bitfield_512_t) {{0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0}};
    bitfield_512_t f = field0;
    pos_t pos;

    int ret = find_unset(&field0, &pos);
    check_equal(ret, 0);
    check_equal(pos.row_number, 0);
    check_equal(pos.bit_number, 0);
    check(equals(&field0, &f), "field should not be changed");

    field0 = (bitfield_512_t) {{0x1,0x0,0x0,0x1,0x0,0x0,0x0,0x0}};
    f = field0;
    ret = find_unset(&field0, &pos);
    check_equal(ret, 0);
    check_equal(pos.row_number, 0);
    check_equal(pos.bit_number, 1);
    check(equals(&field0, &f), "field should not be changed");

    field0 = (bitfield_512_t) {{0x1fffffffff,0x0,0x0,0x1,0x0,0x0,0x0,0xffffffffffffffff}};
    f = field0;
    ret = find_unset(&field0, &pos);
    check_equal(ret, 0);
    check_equal(pos.row_number, 0);
    check_equal(pos.bit_number, 37);
    check(equals(&field0, &f), "field should not be changed");


    field0 = (bitfield_512_t) {{0xffffffffffffffff,0xffffffffffffffff,0x0,0x1,0x0,0xffffffffffffffff,0x0,0x0}};
    f = field0;
    ret = find_unset(&field0, &pos);
    check_equal(ret, 0);
    check_equal(pos.row_number, 2);
    check_equal(pos.bit_number, 0);
    check(equals(&field0, &f), "field should not be changed");


    field0 = (bitfield_512_t) {{0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0x7fffffffffffffff,0x0,0x0,0xffffffffffffffff,0x0}};
    f = field0;
    ret = find_unset(&field0, &pos);
    check_equal(ret, 0);
    check_equal(pos.row_number, 3);
    check_equal(pos.bit_number, 63);
    check(equals(&field0, &f), "field should not be changed");

    field0 = (bitfield_512_t) {{0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0x7fffffffffffffff}};
    f = field0;
    ret = find_unset(&field0, &pos);
    check_equal(ret, 0);
    check_equal(pos.row_number, 7);
    check_equal(pos.bit_number, 63);
    check(equals(&field0, &f), "field should not be changed");

    field0 = (bitfield_512_t) {{0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff,0xffffffffffffffff}};
    f = field0;
    ret = find_unset(&field0, &pos);
    check_equal_m(ret, -1, "should be no space available");
    check(equals(&field0, &f), "field should not be changed");
    


    return sucssess;
}

int bitfield_tests(int* test_counter, int* fail_counter){
    (*test_counter)++;
    printf("Init_field:\n");
    if(!init_field_test()) (*fail_counter)++;
    else printf("\tsuccess\n");

    (*test_counter)++;
    printf("find_unset:\n");
    if(!find_unset_test()) (*fail_counter)++;
    else printf("\tsuccess\n");

    return 0;
}
