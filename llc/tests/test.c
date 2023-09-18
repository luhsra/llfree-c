#include "llc.h"
#include "utils.h"

#include "bitfield_tests.h"
#include "child_tests.h"
#include "lower_tests.h"
#include "local_tests.h"
#include "llc_tests.h"
#include "tree_tests.h"

#include <stdlib.h>

int main()
{
	int test_counter = 0;
	int fail_counter = 0;
	printf("Running Tests\n");
	printf("lockfree 16 Bit algotithms: %s\n",
	       ATOMIC_SHORT_LOCK_FREE ? "True" : "False");
	printf("lockfree 64 Bit algotithms: %s\n",
	       ATOMIC_LLONG_LOCK_FREE ? "True" : "False");

	printf("Running Bitfield Tests:\n");
	bitfield_tests(&test_counter, &fail_counter);
	printf("---------------------------------------\n");

	printf("Running Flag-counter Tests:\n");
	child_tests(&test_counter, &fail_counter);
	printf("---------------------------------------\n");

	printf("Running lower allocator Tests:\n");
	lower_tests(&test_counter, &fail_counter);
	printf("---------------------------------------\n");

	printf("Running tree Tests:\n");
	tree_tests(&test_counter, &fail_counter);
	printf("---------------------------------------\n");

	printf("Running local Tests:\n");
	local_tests(&test_counter, &fail_counter);
	printf("---------------------------------------\n");

	printf("Running llc Tests:\n");
	llc_tests(&test_counter, &fail_counter);
	printf("---------------------------------------\n");

	if (fail_counter == 0)
		printf("----------------SUCCESS----------------\n");
	else
		printf("----------------FAILED----------------\n");
	printf("---------------------------------------\n");
	printf("Failed %d out of %d tests.\n", fail_counter, test_counter);
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
