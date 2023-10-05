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

	check_equal(lf.free_counter, 0);
	check_equal(lf.last_row, 0ul);
	check_equal(reserved.free_counter, 0);
	check_equal(reserved.reserving, false);
	check_equal(reserved.present, false);

	return success;
}

declare_test(local_set_preferred)
{
	bool success = true;
	local_t local;
	local_init(&local);
	reserved_t old_r;

	check(atom_update(&local.reserved, old_r, VOID, local_mark_reserving));
	uint64_t pfn = 45463135;
	unsigned counter = 1 << 13;
	bool ret = atom_update(&local.reserved, old_r,
			       ((reserve_change_t){ pfn, counter }),
			       local_set_reserved);
	check(ret);

	reserved_t reserved = atom_load(&local.reserved);
	check_equal(reserved.start_idx, row_from_pfn(pfn));
	check_equal(reserved.free_counter, counter);
	check(reserved.present);
	check(reserved.reserving);

	reserved_t copy = atom_load(&local.reserved);
	pfn = 454135;
	counter = 9423;
	ret = atom_update(&local.reserved, old_r,
			  ((reserve_change_t){ pfn, counter }),
			  local_set_reserved);
	check(ret);

	reserved = atom_load(&local.reserved);
	check_equal(reserved.start_idx, row_from_pfn(pfn));
	check_equal(reserved.free_counter, counter);
	check(reserved.present);
	check(*((uint64_t *)&old_r) == *((uint64_t *)&copy));

	return success;
}