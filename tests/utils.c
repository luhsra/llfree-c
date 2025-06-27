#include "utils.h"
#include "test.h"

declare_test(for_offsetted)
{
	bool success = true;

	size_t i = 0;
	for_offsetted(0, 32, current_i) {
		check_equal("zu", current_i, i);
		i += 1;
	}

	i = 32;
	for_offsetted(32, 32, current_i) {
		check_equal("zu", current_i, i);
		i += 1;
	}

	i = 33;
	for_offsetted(33, 32, current_i) {
		check_equal("zu", current_i, i);
		i = (i + 1) % 32 + 32;
	}

	i = 31;
	for_offsetted(31, 32, current_i) {
		check_equal("zu", current_i, i);
		i = (i + 1) % 32;
	}

	i = 48;
	for_offsetted(48, 32, current_i) {
		check_equal("zu", current_i, i);
		i = (i + 1) % 32 + 32;
	}

	return success;
}
