#include "check.h"
#include "local.h"
#include "utils.h"

#include <stdint.h>

declare_test(local_atomic)
{
	bool success = true;
	_Atomic(reserved_t) r;
	_Atomic(last_free_t) l;
	check(atomic_is_lock_free(&r));
	check(atomic_is_lock_free(&l));
	return success;
}

declare_test(local_init)
{
	bool success = true;

	local_t actual;
	local_init(&actual);
	last_free_t lf = atom_load(&actual.last_free);
	reserved_t reserved = atom_load(&actual.reserved);

	check_equal(lf.counter, 0);
	check_equal(lf.last_row, 0ul);
	check_equal(reserved.free, 0);
	check_equal(reserved.reserving, false);
	check_equal(reserved.present, false);

	return success;
}
