#include "test.h"

#include "llfree_platform.h"
#include "llfree.h"
#include "llfree_inner.h"
#include "utils.h"

#include <stdlib.h>

struct llkinds {
	llkind_desc_t kinds[4];
};
static struct llkinds make_kinds(size_t cores)
{
	return (struct llkinds){
		.kinds = {
			llkind_desc(llkind(0), cores), // immovable
			llkind_desc(llkind(1), cores), // movable
			llkind_desc(LLKIND_HUGE, cores), // huge
			llkind_desc_zero(), // zero-terminated
		},

	};
}

static inline llflags_t mk_flags(size_t cores, size_t core, size_t order,
				 bool movable)
{
	size_t local = 0;
	if (!movable && order < LLFREE_HUGE_ORDER)
		return llflags(order, local + core % cores);
	local += cores;
	if (order < LLFREE_HUGE_ORDER)
		return llflags(order, local + core % cores);
	local += cores;
	return llflags(order, local + core % cores);
}

static llfree_t llfree_new(size_t cores, size_t frames, uint8_t init)
{
	struct llkinds kinds = make_kinds(cores);
	llfree_t upper;
	llfree_meta_size_t m = llfree_metadata_size(kinds.kinds, frames);
	llfree_meta_t meta = {
		.local = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.local),
		.trees = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.trees),
		.lower = llfree_ext_alloc(LLFREE_CACHE_SIZE, m.lower),
	};
	llfree_result_t ll_unused ret =
		llfree_init(&upper, kinds.kinds, frames, init, meta);
	assert(llfree_is_ok(ret));
	return upper;
}

static void llfree_drop(llfree_t *self)
{
	llfree_meta_t m = llfree_metadata(self);
	llfree_ext_free(m.local);
	llfree_ext_free(m.trees);
	llfree_ext_free(m.lower);
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
#define mk_flags(core, order, movable) mk_flags(4, core, order, movable)

	const size_t len = 4 * LLFREE_TREE_SIZE;
	uint64_t *frames = calloc(len, sizeof(uint64_t));

	llfree_result_t movable =
		llfree_get(&llfree, ll_none(), mk_flags(0, 0, true));
	check_m(llfree_is_ok(movable), "movable allocation failed");
	llfree_result_t normal =
		llfree_get(&llfree, ll_none(), mk_flags(0, 0, false));
	check_m(llfree_is_ok(normal), "normal allocation failed");

	for (size_t i = 0; i < len; i++) {
		llfree_result_t ret =
			llfree_get(&llfree, ll_none(), mk_flags(0, 0, i % 2));
		check_m(llfree_is_ok(ret), "movable allocation %zu failed", i);
		frames[i] = ret.frame;
	}

	llfree_print(&llfree);
	llfree_print_debug(&llfree, writer, NULL);
	llfree_warn("freeing some");

	llfree_result_t ret =
		llfree_put(&llfree, movable.frame, mk_flags(0, 0, true));
	check_m(llfree_is_ok(ret), "free movable failed");
	ret = llfree_put(&llfree, normal.frame, mk_flags(0, 0, false));
	check_m(llfree_is_ok(ret), "free normal failed");

	llfree_print(&llfree);
	llfree_print_debug(&llfree, writer, NULL);
	llfree_warn("freeing all");

	for (size_t i = 0; i < len; i++) {
		llfree_result_t ret =
			llfree_put(&llfree, frames[i], mk_flags(0, 0, false));
		check_m(llfree_is_ok(ret), "free allocation %zu failed", i);
	}

	llfree_print(&llfree);
	llfree_print_debug(&llfree, writer, NULL);

	return success;
}
