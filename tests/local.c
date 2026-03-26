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

#if LLFREE_ENABLE_FREE_RESERVE
declare_test(local_last_free_inc)
{
	bool success = true;

	// Simple 2-tier tiering: tier 0 (small, 1 slot), tier 1 (huge, 1 slot)
	llfree_tiering_t tiering = llfree_tiering_simple(1);
	local_t *local =
		llfree_ext_alloc(LLFREE_CACHE_SIZE, ll_local_size(&tiering));
	ll_local_init(local, &tiering);

	// tier 0, index 0 (first slot of tier 0)
	uint8_t tier = 0;
	size_t index = 0;
	for (size_t i = 0; i < LAST_FREES; i++) {
		check(!ll_local_free_inc(local, tier, index, tree_id(0)));
	}
	check(ll_local_free_inc(local, tier, index, tree_id(0)));
	check(ll_local_free_inc(local, tier, index, tree_id(0)));

	check(!ll_local_free_inc(local, tier, index, tree_id(1)));
	check(!ll_local_free_inc(local, tier, index, tree_id(0)));

	for (size_t i = 0; i < LAST_FREES; i++) {
		check(!ll_local_free_inc(local, tier, index, tree_id(1)));
	}
	check(ll_local_free_inc(local, tier, index, tree_id(1)));
	llfree_ext_free(LLFREE_CACHE_SIZE, ll_local_size(&tiering), local);
	return success;
}
#endif
