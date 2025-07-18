#include "llfree.h"
#include "test.h"
#include "utils.h"

#include <stdlib.h>
#include <string.h>

#define TEST_CASE_N 100
struct test_case {
	char *name;
	bool (*f)(void);
};

static size_t test_case_i;
static struct test_case TESTS[TEST_CASE_N] = { 0 };

int main(int argc, char **argv)
{
	size_t fail_counter = 0;
	size_t failed_tests[TEST_CASE_N] = { 0 };

	printf("\x1b[92mRunning %zu test cases...\x1b[0m\n", test_case_i);

	for (size_t i = 0; i < test_case_i; i++) {
		if (argc == 2 && strstr(TESTS[i].name, argv[1]) == NULL) {
			printf("\x1b[90mIgnore test '%s'\x1b[0m\n",
			       TESTS[i].name);
			continue;
		}

		printf("\x1b[92mRunning test '%s'\x1b[0m\n", TESTS[i].name);
		if (!(TESTS[i].f)()) {
			failed_tests[fail_counter] = i;
			fail_counter += 1;
		}
	}

	if (fail_counter == 0) {
		printf("\x1b[92m----------------SUCCESS----------------\x1b[0m\n");
	} else {
		printf("\x1b[91m----------------FAILED-----------------\x1b[0m\n");
		printf("Failed %zu out of %zu tests.\n", fail_counter,
		       test_case_i);
		for (size_t i = 0; i < fail_counter; i++) {
			printf("\t\x1b[91m%s\x1b[0m\n",
			       TESTS[failed_tests[i]].name);
		}
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

void add_test(char *name, bool (*f)(void))
{
	assert(test_case_i < TEST_CASE_N);
	TESTS[test_case_i].name = name;
	TESTS[test_case_i].f = f;
	test_case_i++;
}

void *llfree_ext_alloc(size_t align, size_t size)
{
	llfree_info("alloc a=%zu %zu", align, size);
	void *ret = aligned_alloc(align, align_up(size, align));
	assert(ret != NULL);
	return ret;
}

void llfree_ext_free(ll_unused size_t align, ll_unused size_t size, void *addr)
{
	return free(addr);
}
