#pragma once

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#define run_test(test_func)        \
	(*test_counter)++;         \
	printf(#test_func ":\n");  \
	if (!test_func())          \
		(*fail_counter)++; \
	else                       \
		printf("\tsuccess\n")

#define check(x, msg)                                                      \
	if (!(x)) {                                                        \
		printf("\tFILE %s:%d:\n\tCheck %s failed: %s\n", __FILE__, \
		       __LINE__, #x, msg);                                 \
		success = false;                                           \
	}                                                                  \
	(void)"" // enforce semicolon!

#define _check_msg_prefix \
	"\tFILE %s:%d: Check %s == %s failed: %s\n\texpected: "
#define _check_msg_mid ", actual value: "
#define _check_msg_suffix \
	"\tFILE %s:%d: Check %s == %s failed: %s\n\texpected: "

#define fmt_spec(x)                    \
	_Generic((x),                  \
		_Bool: "%d",           \
		char: "%c",            \
		unsigned char: "%c",   \
		short: "%hd",          \
		unsigned short: "%hu", \
		int: "%d",             \
		unsigned int: "%u",    \
		long: "%ld",           \
		unsigned long: "%lu",  \
		long long: "%lld",     \
		unsigned long long: "%llu")

#define check_equal_m(actual, expected, msg)                                    \
	if ((actual) != (expected)) {                                           \
		printf("\tFILE %s:%d: Check %s == %s failed: %s\n\texpected: ", \
		       __FILE__, __LINE__, #actual, #expected, msg);            \
		printf(fmt_spec(expected), (expected));                           \
		printf(", actual value: ");                                     \
		printf(fmt_spec(actual), (actual));                             \
		printf("\n");                                                   \
		success = false;                                                \
	}                                                                       \
	(void)"" // enforce semicolon!

#define check_equal(actual, expected) check_equal_m(actual, expected, "")

#define check_equal_bitfield_m(actual, expected, msg)                                 \
	if (!field_equals(&actual, &expected)) {                                      \
		printf("\tFILE %s:%d: Check equal bitfields failed: %s\n\tactual:\n", \
		       __FILE__, __LINE__, msg);                                      \
		field_print(&actual);                                                 \
		printf("\texpected:\n");                                              \
		field_print(&expected);                                               \
		success = false;                                                      \
	}                                                                             \
	(void)"" // enforce semicolon!

#define check_equal_bitfield(actual, expected) \
	check_equal_bitfield_m(actual, expected, "")
