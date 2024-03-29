#include "check.h"
#include "local.h"
#include "utils.h"

declare_test(local_atomic)
{
	bool success = true;
	_Atomic(bool) r;
	check(atomic_is_lock_free(&r));
	return success;
}

declare_test(local_last_free_inc)
{
	bool success = true;

	local_t local;
	ll_local_init(&local);

	for (size_t i = 0; i < LAST_FREES; i++) {
		check(!ll_local_free_inc(&local, 0));
	}
	check(ll_local_free_inc(&local, 0));
	check(ll_local_free_inc(&local, 0));

	check(!ll_local_free_inc(&local, 1));
	check(!ll_local_free_inc(&local, 0));

	for (size_t i = 0; i < LAST_FREES; i++) {
		check(!ll_local_free_inc(&local, 1));
	}
	check(ll_local_free_inc(&local, 1));
	return success;
}
