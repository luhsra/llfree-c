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

	// Simple 2-class classing: class 0 (small, 1 slot), class 1 (huge, 1 slot)
	llfree_classing_t classing = llfree_classing_simple(1);
	local_t *local =
		llfree_ext_alloc(LLFREE_CACHE_SIZE, ll_local_size(&classing));
	ll_local_init(local, &classing);

	// class 0, index 0 (first slot of class 0)
	uint8_t class = 0;
	size_t index = 0;
	for (size_t i = 0; i < LAST_FREES; i++) {
		check(!ll_local_free_inc(local, class, index, tree_id(0)));
	}
	check(ll_local_free_inc(local, class, index, tree_id(0)));
	check(ll_local_free_inc(local, class, index, tree_id(0)));

	check(!ll_local_free_inc(local, class, index, tree_id(1)));
	check(!ll_local_free_inc(local, class, index, tree_id(0)));

	for (size_t i = 0; i < LAST_FREES; i++) {
		check(!ll_local_free_inc(local, class, index, tree_id(1)));
	}
	check(ll_local_free_inc(local, class, index, tree_id(1)));
	llfree_ext_free(LLFREE_CACHE_SIZE, ll_local_size(&classing), local);
	return success;
}
#endif
