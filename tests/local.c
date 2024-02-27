#include "check.h"
#include "local.h"
#include "utils.h"

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
	check_equal(lf.tree_idx, 0ul);
	check_equal(reserved.free, 0lu);
	check_equal(reserved.lock, false);
	check_equal(reserved.present, false);

	return success;
}

declare_test(local_last_free_inc)
{
	bool success = true;

	_Atomic(last_free_t) lf = (last_free_t){ 0, 0 };

	last_free_t old;
	check(atom_update(&lf, old, last_free_inc, 0)); // 1
	check(atom_update(&lf, old, last_free_inc, 0)); // 2
	check(atom_update(&lf, old, last_free_inc, 0)); // 3
	check(atom_update(&lf, old, last_free_inc, 0)); // 4
	check(!atom_update(&lf, old, last_free_inc, 0)); // 0
	check(!atom_update(&lf, old, last_free_inc, 0)); // 0

	check(atom_update(&lf, old, last_free_inc, 1)); // 0
	check(atom_update(&lf, old, last_free_inc, 0)); // 0

	check(atom_update(&lf, old, last_free_inc, 1)); // 0
	check(atom_update(&lf, old, last_free_inc, 1)); // 1
	check(atom_update(&lf, old, last_free_inc, 1)); // 2
	check(atom_update(&lf, old, last_free_inc, 1)); // 3
	check(atom_update(&lf, old, last_free_inc, 1)); // 4
	check(!atom_update(&lf, old, last_free_inc, 1));
	return success;
}
