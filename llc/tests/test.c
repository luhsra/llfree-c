#include "llc.h"
#include "tests/check.h"
#include "utils.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define test_case_n 100
struct test_case {
	char *name;
	bool (*f)();
};

static size_t test_case_i;
static struct test_case TESTS[test_case_n] = { 0 };

int main()
{
	size_t fail_counter = 0;

	printf("\x1b[92mRunning %lu test cases...\x1b[0m\n", test_case_i);

	for (size_t i = 0; i < test_case_i; i++) {
		printf("\x1b[92mRunning test '%s'\x1b[0m\n", TESTS[i].name);
		if (!(TESTS[i].f)()) {
			fail_counter += 1;
		}
	}

	if (fail_counter == 0) {
		printf("\x1b[92m----------------SUCCESS----------------\x1b[0m\n");
	} else {
		printf("\x1b[91m----------------FAILED-----------------\x1b[0m\n");
		printf("Failed %lu out of %lu tests.\n", fail_counter,
		       test_case_i);
		return 1;
	}
	return 0;
}

declare_test(atomicity)
{
	bool success = true;
	check(ATOMIC_SHORT_LOCK_FREE);
	check(ATOMIC_LLONG_LOCK_FREE);
	return success;
}

void add_test(char *name, bool (*f)())
{
	assert(test_case_i < test_case_n);
	TESTS[test_case_i].name = name;
	TESTS[test_case_i].f = f;
	test_case_i++;
}

void *llc_ext_alloc(size_t align, size_t size)
{
	info("alloc a=%lu %lu -> %lu", align, size, align_up(align, size));
	return aligned_alloc(align, align_up(align, size));
}

void llc_ext_free(_unused size_t align, _unused size_t size, void *addr)
{
	return free(addr);
}
