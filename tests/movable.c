#include "test.h"

#include "llfree_platform.h"
#include "llfree.h"
#include "llfree_inner.h"
#include "utils.h"

#include <stdlib.h>

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

static void writer(void *arg, const char *msg)
{
        (void)arg;
	printf("%s", msg);
}

declare_test(alloc_movable)
{
	bool success = true;

	llfree_t llfree lldrop =
		llfree_new(4, (1 << 30) / LLFREE_FRAME_SIZE, LLFREE_INIT_FREE);

	llflags_t flags = llflags(0);
	flags.movable = true;

	const size_t len = 4 * LLFREE_TREE_SIZE;
	uint64_t *frames = calloc(len, sizeof(uint64_t));

	llfree_result_t movable = llfree_get(&llfree, 0, flags);
	check_m(llfree_is_ok(movable), "movable allocation failed");
	llfree_result_t normal = llfree_get(&llfree, 0, llflags(0));
	check_m(llfree_is_ok(normal), "normal allocation failed");

	for (size_t i = 0; i < len; i++) {
		llflags_t flags = llflags(0);
		flags.movable = i % 2 ? true : false;
		llfree_result_t ret = llfree_get(&llfree, 0, flags);
		check_m(llfree_is_ok(ret), "movable allocation %zu failed", i);
		frames[i] = ret.frame;
	}

	llfree_print(&llfree);
	llfree_print_debug(&llfree, writer, NULL);
	llfree_warn("freeing some");

	llfree_result_t ret = llfree_put(&llfree, 0, movable.frame, llflags(0));
	check_m(llfree_is_ok(ret), "free movable failed");
	ret = llfree_put(&llfree, 0, normal.frame, llflags(0));
	check_m(llfree_is_ok(ret), "free normal failed");

	llfree_print(&llfree);
	llfree_print_debug(&llfree, writer, NULL);
	llfree_warn("freeing all");

	for (size_t i = 0; i < len; i++) {
		llfree_result_t ret =
			llfree_put(&llfree, 0, frames[i], llflags(0));
		check_m(llfree_is_ok(ret), "free allocation %zu failed", i);
	}

	llfree_print(&llfree);
	llfree_print_debug(&llfree, writer, NULL);

	return success;
}
