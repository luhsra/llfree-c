#include "test.h"

#include "llfree_platform.h"
#include "llfree.h"
#include "llfree_inner.h"

#include <stdlib.h>

// Helper macros for movable tiering requests
#define ll_cores(self) ll_local_tier_locals((self)->local, 0)
#define llreq(self, core, order) \
	llfree_movable_request(ll_cores(self), (uint8_t)(order), core, false)
#define llreq_mov(self, core, order) \
	llfree_movable_request(ll_cores(self), (uint8_t)(order), core, true)

static llfree_t llfree_new(size_t cores, size_t frames, uint8_t init)
{
	llfree_t upper;
	llfree_tiering_t tiering = llfree_tiering_movable(cores);
	llfree_meta_size_t m = llfree_metadata_size(&tiering, frames);
	llfree_meta_t meta = {
		.local = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.local),
		.trees = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.trees),
		.lower = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.lower),
	};
	llfree_result_t ll_unused ret =
		llfree_init(&upper, frames, init, meta, &tiering);
	assert(llfree_is_ok(ret));
	return upper;
}

static void llfree_drop(llfree_t *self)
{
	llfree_meta_size_t ms = llfree_metadata_size_of(self);
	llfree_meta_t m = llfree_metadata(self);
	llfree_ext_free(LLFREE_CACHE_SIZE, ms.local, m.local);
	llfree_ext_free(LLFREE_CACHE_SIZE, ms.trees, m.trees);
	llfree_ext_free(LLFREE_CACHE_SIZE, ms.lower, m.lower);
}
#define lldrop __attribute__((cleanup(llfree_drop)))

declare_test(alloc_movable)
{
	bool success = true;

	llfree_t llfree lldrop =
		llfree_new(4, (1 << 30) / LLFREE_FRAME_SIZE, LLFREE_INIT_FREE);

	// Remove the old llflags_t flags = llflags(0); flags.movable = true; block
	const size_t len = 4 * LLFREE_TREE_SIZE;
	uint64_t *frames = calloc(len, sizeof(uint64_t));

	llfree_result_t movable = llfree_get(&llfree, frame_id_none(), llreq_mov(&llfree, 0, 0));
	check_m(llfree_is_ok(movable), "movable allocation failed");
	llfree_result_t normal = llfree_get(&llfree, frame_id_none(), llreq(&llfree, 0, 0));
	check_m(llfree_is_ok(normal), "normal allocation failed");

	for (size_t i = 0; i < len; i++) {
		bool mov = i % 2 ? true : false;
		llfree_result_t ret = llfree_get(
			&llfree, frame_id_none(),
			llfree_movable_request(ll_cores(&llfree), 0, 0, mov));
		check_m(llfree_is_ok(ret), "movable allocation %zu failed", i);
		frames[i] = ret.frame.value;
	}

	// llfree_print(&llfree);
	// llfree_print_debug(&llfree, writer, NULL);
	llfree_warn("freeing some");

	check(llfree_tree_stats(&llfree).free_frames ==
	      (1 << 30) / LLFREE_FRAME_SIZE - len - 2);

	llfree_result_t ret =
		llfree_put(&llfree, frame_id(movable.frame.value), llreq(&llfree, 0, 0));
	check_m(llfree_is_ok(ret), "free movable failed");
	ret = llfree_put(&llfree, frame_id(normal.frame.value), llreq(&llfree, 0, 0));
	check_m(llfree_is_ok(ret), "free normal failed");

	// llfree_print(&llfree);
	// llfree_print_debug(&llfree, writer, NULL);
	llfree_warn("freeing all");

	for (size_t i = 0; i < len; i++) {
		llfree_result_t ret =
			llfree_put(&llfree, frame_id(frames[i]), llreq(&llfree, 0, 0));
		check_m(llfree_is_ok(ret), "free allocation %zu failed", i);
	}

	check(llfree_tree_stats(&llfree).free_frames ==
	      (1 << 30) / LLFREE_FRAME_SIZE);

	// llfree_print(&llfree);
	// llfree_print_debug(&llfree, writer, NULL);

	return success;
}
