#include "test.h"
#include "local.h"
#include "llfree.h"

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

	// Simple 2-tier tiering: tier 0 (small, 1 slot), tier 1 (huge, 1 slot)
	llfree_tiering_t tiering = llfree_tiering_simple(1);
	local_t *local =
		llfree_ext_alloc(LLFREE_CACHE_SIZE, ll_local_size(&tiering));
	ll_local_init(local, &tiering);

	// slot 0 = tier 0, core 0 (offset 0 in simple tiering with 1 core)
	size_t slot = 0;
	for (size_t i = 0; i < LAST_FREES; i++) {
		check(!ll_local_free_inc(local, slot, 0));
	}
	check(ll_local_free_inc(local, slot, 0));
	check(ll_local_free_inc(local, slot, 0));

	check(!ll_local_free_inc(local, slot, 1));
	check(!ll_local_free_inc(local, slot, 0));

	for (size_t i = 0; i < LAST_FREES; i++) {
		check(!ll_local_free_inc(local, slot, 1));
	}
	check(ll_local_free_inc(local, slot, 1));
	llfree_ext_free(LLFREE_CACHE_SIZE, ll_local_size(&tiering), local);
	return success;
}
