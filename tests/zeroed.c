#include "linux/types.h"
#include "llfree.h"
#include "llfree_inner.h"
#include "test.h"
#include "llfree_platform.h"

static llfree_t llfree_new(size_t cores, size_t frames, uint8_t init)
{
	llfree_t upper;
	llfree_meta_size_t m = llfree_metadata_size(cores, frames);
	llfree_meta_t meta = {
		.local = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.local),
		.trees = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.trees),
		.lower = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.lower),
	};
	llfree_result_t ll_unused ret =
		llfree_init(&upper, cores, frames, init, meta);
	assert(llfree_is_ok(ret));
	return upper;
}

static void llfree_drop(llfree_t *self)
{
	llfree_meta_size_t ms =
		llfree_metadata_size(llfree_cores(self), llfree_frames(self));
	llfree_meta_t m = llfree_metadata(self);
	llfree_ext_free(LLFREE_CACHE_SIZE, ms.local, m.local);
	llfree_ext_free(LLFREE_CACHE_SIZE, ms.trees, m.trees);
	llfree_ext_free(LLFREE_CACHE_SIZE, ms.lower, m.lower);
}
#define lldrop __attribute__((cleanup(llfree_drop)))

declare_test(zeroed_basic)
{
	bool success = true;

	const size_t FRAMES = 12 * LLFREE_TREE_SIZE;
	lldrop llfree_t upper = llfree_new(2, FRAMES, LLFREE_INIT_FREE);
	llfree_validate(&upper);
	check(llfree_frames(&upper) == FRAMES);
	check(llfree_stats(&upper).free_frames == FRAMES);

	// try allocate a zero page while none are available
	llflags_t flags = llflags(LLFREE_CHILD_ORDER);
	flags.zeroed = true;
	llfree_result_t res = llfree_get(&upper, 0, flags);
	check(llfree_is_ok(res));
	check(!res.zeroed);
	llfree_validate(&upper);
	llfree_print(&upper);

	// zero some memory...
	res = llfree_reclaim(&upper, 0, true, true, true);
	check(llfree_is_ok(res));
	check(!res.zeroed);
	// zeroing operation...
	res = llfree_return(&upper, res.frame, true);
	check(llfree_is_ok(res));

	// now we should be able to get a zeroed page
	res = llfree_get(&upper, 0, flags);
	check(llfree_is_ok(res));
	check(res.zeroed);

	return success;
}

declare_test(zeroed_all)
{
	bool success = true;

	const size_t FRAMES = 12 * LLFREE_TREE_SIZE;
	lldrop llfree_t upper = llfree_new(2, FRAMES, LLFREE_INIT_FREE);
	llfree_validate(&upper);
	check(llfree_frames(&upper) == FRAMES);
	check(llfree_stats(&upper).free_frames == FRAMES);

	llfree_info("Zeroing all pages");

	// zero everything
	for (size_t i = 0; i < FRAMES; i += LLFREE_CHILD_SIZE) {
		llfree_result_t res = llfree_reclaim(&upper, i, true, true, true);
		check(llfree_is_ok(res));
		if (res.zeroed) llfree_print(&upper);
		check_m(!res.zeroed, "frame %zu should not be zeroed",
			i / LLFREE_CHILD_SIZE);
		// zeroing operation...
		res = llfree_return(&upper, res.frame, true);
		check(llfree_is_ok(res));
	}

	llfree_info("All pages zeroed");
	llfree_validate(&upper);
	check(llfree_stats(&upper).zeroed_huge == FRAMES / LLFREE_CHILD_SIZE);

	// now there should only be zeroed pages
	llfree_result_t res = llfree_reclaim(&upper, 0, true, true, true);
	check(!llfree_is_ok(res) && res.error == LLFREE_ERR_MEMORY);

	// now we should be able to all pages as zeroed
	for (size_t i = 0; i < FRAMES; i += LLFREE_CHILD_SIZE) {
		llflags_t flags = llflags(LLFREE_CHILD_ORDER);
		flags.zeroed = true;
		llfree_result_t res = llfree_get(&upper, 0, flags);
		check(llfree_is_ok(res));
		check(res.zeroed);
	}

	llfree_info("All pages allocated");
	llfree_print(&upper);
	check(llfree_stats(&upper).free_frames == 0);
	llfree_validate(&upper);

	// free half of the zeroed pages -> they become dirty
	for (size_t i = 0; i < FRAMES / 2; i += LLFREE_CHILD_SIZE) {
		llfree_result_t res = llfree_put(&upper, 0, i, llflags(LLFREE_CHILD_ORDER));
		check(llfree_is_ok(res));
	}

	check(llfree_stats(&upper).zeroed_huge == 0);

	llfree_info("No zeroed pages left");

	// allocation should now return a non-zeroed page
	llflags_t flags = llflags(LLFREE_CHILD_ORDER);
	flags.zeroed = true;

	for (size_t i = 0; i < FRAMES / 2; i += LLFREE_CHILD_SIZE) {
		llfree_result_t res = llfree_get(&upper, 0, flags);
		check(llfree_is_ok(res));
		check(!res.zeroed);
	}

	llfree_validate(&upper);

	return success;
}
