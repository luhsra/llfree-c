#pragma once

#include "utils.h"
#include "bitfield.h"

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#define run_test(test_func)        \
	(*test_counter)++;         \
	printf(#test_func ":\n");  \
	if (!test_func())          \
		(*fail_counter)++; \
	else                       \
		printf("\t\x1b[92msuccess\x1b[0m\n")

#define check_m(x, msg)                                               \
	if (!(x)) {                                                 \
		printf("\x1b[91m%s:%d failed", __FILE__, __LINE__); \
		if (msg && msg[0])                                  \
			printf(" (%s)", msg);                       \
		printf("\x1b[0m\n");                                \
		success = false;                                    \
	}                                                           \
	(void)0 // enforce semicolon!

#define check(x) check_m(x, "")

#define fmt_spec(x)                           \
	_Generic((x), _Bool                   \
		 : "%d", char                 \
		 : "%c", unsigned char        \
		 : "%c", short                \
		 : "%hd", unsigned short      \
		 : "%hu", int                 \
		 : "%d", unsigned int         \
		 : "%u", long                 \
		 : "%ld", unsigned long       \
		 : "%lu", long long           \
		 : "%lld", unsigned long long \
		 : "%llu")

#define check_equal_m(actual, expected, msg)                           \
	if ((actual) != (expected)) {                                  \
		printf("\x1b[91m%s:%d failded: ", __FILE__, __LINE__); \
		printf(fmt_spec(expected), (expected));                \
		printf(" == ");                                        \
		printf(fmt_spec(actual), (actual));                    \
		if (msg && msg[0])                                     \
			printf(" (%s)", msg);                          \
		printf("\x1b[0m\n");                                   \
		success = false;                                       \
	}                                                              \
	(void)0 // enforce semicolon!

#define check_equal(actual, expected) check_equal_m(actual, expected, "")

#define check_equal_bitfield_m(actual, expected, msg)                      \
	if (!field_equals(&actual, &expected)) {                           \
		printf("\x1b[91m%s:%d: failed: %s\n\tactual:\n", __FILE__, \
		       __LINE__, msg);                                     \
		field_print(&actual);                                      \
		printf("\texpected:\n");                                   \
		field_print(&expected);                                    \
		printf("\x1b[0m");                                         \
		success = false;                                           \
	}                                                                  \
	(void)0 // enforce semicolon!

static inline _unused bool field_equals(bitfield_t *f1, bitfield_t *f2)
{
	if (f1 == NULL || f2 == NULL)
		return false;
	for (size_t i = 0; i < FIELD_N; i++) {
		if (f1->rows[i] != f2->rows[i])
			return false;
	}
	return true;
}

#define check_equal_bitfield(actual, expected) \
	check_equal_bitfield_m(actual, expected, "")
