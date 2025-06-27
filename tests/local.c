#include "test.h"
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

	local_t *local = llfree_ext_alloc(LLFREE_CACHE_SIZE, ll_local_size(1));
	ll_local_init(local, 1);

	for (size_t i = 0; i < LAST_FREES; i++) {
		check(!ll_local_free_inc(local, 0, 0));
	}
	check(ll_local_free_inc(local, 0, 0));
	check(ll_local_free_inc(local, 0, 0));

	check(!ll_local_free_inc(local, 0, 1));
	check(!ll_local_free_inc(local, 0, 0));

	for (size_t i = 0; i < LAST_FREES; i++) {
		check(!ll_local_free_inc(local, 0, 1));
	}
	check(ll_local_free_inc(local, 0, 1));
	return success;
}
