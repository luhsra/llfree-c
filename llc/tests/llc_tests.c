#include "llc_tests.h"
#include "../llc.h"
#include "check.h"

#include "assert.h"
#include <stdbool.h>

void print_trees(upper_t *self) {
  printf("TREES:\nIDX\tFlag\tCounter\n");
  for (size_t i = 0; i < self->num_of_trees; ++i) {
    printf("%lu\t%d\t%X\n", i, self->trees[i].flag, self->trees[i].counter);
  }
}

void print_locale(upper_t *self) {
  printf("LOCALE:\nidx\ttreeIDXtcounter\tfreeTDX\tcntr\n");
  for (size_t i = 0; i < self->cores; ++i) {
    printf("%lu\t%lu\t%d\t%lu\t%d\n", i,
           getTreeIdx(get_reserved_tree_index(&self->local[i])),
           self->local[i].reserved.free_counter,
           getTreeIdx(get_last_free_tree_index(&self->local[i])),
           self->local[i].last_free.free_counter);
  }
}

bool first_function_test() {
  bool success = true;

  upper_t *upper = (upper_t *)llc_default();
  int64_t ret = llc_init(upper, 4, 0, 132000, 0, false);
  check(ret == ERR_OK, "init is success");
  check(upper->num_of_trees == 9, "");
  check(upper->cores == 4, "");
  check(llc_frames(upper) == 132000, "");
  check(llc_free_frames(upper) == 132000, "right number of free frames")

      printf("AFTER INITIALIZATION\n");
  print_locale(upper);
  print_trees(upper);

  ret = llc_get(upper, 2, 0);
  check(ret == ERR_OK, "reservation must be success");

  printf("AFTER GET\n");
  print_trees(upper);
  print_locale(upper);

  check(llc_frames(upper) == 132000, "");
  check(llc_free_frames(upper) == 131999, "right number of free frames")

      check(upper->trees[0].flag, "tree is reserved now");
  check(upper->trees[0].counter == 0, "counter was copied to local");
  check(upper->local[2].reserved.has_reserved_tree, "");
  check(upper->local[2].reserved.free_counter == 16383,
        "local counter is decreased");

  check(upper->lower.childs[0].counter == 511, "");
  check(upper->lower.childs[1].counter == 512, "");
  check(upper->lower.fields[0].rows[0] == 0x01, "");
  check(upper->lower.fields[1].rows[1] == 0x00, "");

  ret = llc_put(upper, 2, ret, 0);

  printf("AFTER PUT\n");
  print_trees(upper);
  print_locale(upper);

  check(ret == ERR_OK, "successfully free");
  check(llc_free_frames(upper) == 132000, "right number of free frames")
      check(upper->lower.childs[0].counter == 512, "free in childs");
  check(upper->lower.fields[0].rows[0] == 0x0, "free in bitfields");
  check_equal_m(upper->local[2].reserved.free_counter, 16384,
                "local counter is increased");
  check(upper->trees[0].counter == 0, "ccounter in tree array is not touched");

  return success;
}

int llc_tests(int *test_counter, int *fail_counter) {
  run_test(first_function_test);

  return 0;
}