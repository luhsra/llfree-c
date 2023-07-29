#include "local_tests.h"
#include "../local.h"
#include "check.h"
#include "enum.h"
#include "pfn.h"

bool init_local_test() {
  bool success = true;

  local_t actual;
  init_local(&actual);
  check_equal(actual.last_free.free_counter, 0);
  check_uequal(actual.last_free.last_free_idx, 0ul);
  check_equal(actual.reserved.free_counter, 0);
  check_equal(actual.reserved.reservation_in_progress, false);
  check_equal(actual.reserved.has_reserved_tree, false);

  return success;
}

bool set_preferred_test() {
  bool success = true;
  local_t local_o;
  local_t *local = &local_o;
  init_local(local);
  mark_as_searchig(local);
  uint64_t pfn = 45463135;
  unsigned counter = 1 << 13;
  reserved_t old;
  int ret = set_preferred(local, pfn, counter, &old);
  check(ret == ERR_OK, "");
  check_uequal(local->reserved.preferred_index, atomic_from_pfn(pfn));
  check_equal(local->reserved.free_counter, counter);
  check(local->reserved.has_reserved_tree, "");

  mark_as_searchig(local);
  local_t copy = local_o;
  pfn = 454135;
  counter = 9423;
  ret = set_preferred(local, pfn, counter, &old);
  check(ret == ERR_OK, "");
  check_uequal(local->reserved.preferred_index, atomic_from_pfn(pfn));
  check_equal(local->reserved.free_counter, counter);
  check(local->reserved.has_reserved_tree, "");
  check(old.raw == copy.reserved.raw, "");

  return success;
}

int local_tests(int *test_counter, int *fail_counter) {
  run_test(init_local_test);
  run_test(set_preferred_test);
  return 0;
}