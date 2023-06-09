#include "llc_tests.h"
#include "../llc.h"
#include "check.h"

#include "assert.h"
#include "../enum.h"
#include "../pfn.h"
#include "../utils.h"
#include <stdbool.h>
#include <stdlib.h>

void print_trees(upper_t *self) {
  printf("TREES:\nIDX\tFlag\tCounter\n");
  for (size_t i = 0; i < self->num_of_trees; ++i) {
    p("%lu\t%d\t%X\n", i, self->trees[i].flag, self->trees[i].counter);
  }
}


bool general_function_test() {
  bool success = true;

  upper_t *upper = (upper_t *)llc_default();
  int64_t ret = llc_init(upper, 4, 0, 132000, 0, true);
  check(ret == ERR_OK, "init is success");
  check(upper->num_of_trees == 9, "");
  check(upper->cores == 4, "");
  check(llc_frames(upper) == 132000, "");
  check(llc_free_frames(upper) == 132000, "right number of free frames");

  p("Before get\n");
  llc_print(upper);

  ret = llc_get(upper, 0, 0);
  check(ret >= 0, "reservation must be success");

  check(llc_frames(upper) == 132000, "");
  check(llc_free_frames(upper) == 131999, "right number of free frames");

  check(upper->trees[0].flag, "tree is reserved now");
  check(upper->trees[0].counter == 0, "counter was copied to local");
  check(upper->local[0].reserved.has_reserved_tree, "");
  check(upper->local[0].reserved.free_counter == 16383,
        "local counter is decreased");

  check(upper->lower.childs[0].counter == 511, "");
  check(upper->lower.childs[1].counter == 512, "");
  check(upper->lower.fields[0].rows[0] == 0x01, "");
  check(upper->lower.fields[1].rows[1] == 0x00, "");

  p("After get mit core 0\n");
  llc_print(upper);

  int64_t frame = ret;
  ret = llc_put(upper, 0, frame, 0);

  check(ret == ERR_OK, "successfully free");
  check(llc_free_frames(upper) == 132000, "right number of free frames");
  check(upper->lower.childs[0].counter == 512, "free in childs");
  check(upper->lower.fields[0].rows[0] == 0x0, "free in bitfields");
  check_equal_m(upper->local[0].reserved.free_counter, 16384,
                "local counter is increased");
  check(upper->trees[0].counter == 0, "counter in tree array is not touched");

  check_uequal(upper->local->last_free.last_free_idx, getAtomicIdx(frame));
  check_equal(upper->local->last_free.free_counter, 0);




  //reserve all frames in first tree
  for (int i = 0; i < TREESIZE; ++i){
    ret = llc_get(upper, 0, 0);
    check(ret >= 0, "");
  }
  
  check(ret >= 0, "");


  //reserve first frame in new tree
  ret = llc_get(upper, 0, 0);
  check(ret >= 0, "");
  check(upper->local[0].reserved.preferred_index = getAtomicIdx(TREESIZE), "second tree must be allocated");

  llc_print(upper);

  llc_drop(upper);
  return success;
}

bool check_init(upper_t *upper, size_t cores, pfn_at start_pfn, size_t len,
                uint8_t init, uint8_t free_all) {
  bool success = true;
  int ret = llc_init(upper, cores, start_pfn, len, init, free_all);
  size_t num_trees = div_ceil(len, TREESIZE);
  size_t num_childs = div_ceil(len, CHILDSIZE);

  check(ret == ERR_OK, "init is success");
  check_uequal(upper->num_of_trees, num_trees);
  check_uequal(upper->lower.num_of_childs, num_childs);
  check_uequal(upper->lower.start_pfn, start_pfn);
  check_uequal(upper->cores, (cores > num_trees ? num_trees : cores));
  check_uequal(llc_frames(upper), len);
  check_uequal(llc_free_frames(upper), free_all ? len : 0);
  check_uequal(upper->local[0].reserved.raw, 0ul);

  return success;
}

bool init_llc_test() {
  int success = true;

  upper_t *upper = llc_default();
  check(upper != NULL, "default init must reserve memory");

  if (!check_init(upper, 4, 0, 1 << 20, VOLATILE, true)) {
    success = false;
  }

  llc_drop(upper);
  return success;
}



bool test_put(){
  bool success = true;
  upper_t *upper = (upper_t *)llc_default();
  int64_t ret = llc_init(upper, 4, 0, 132000, 0, true);
  assert(ret == ERR_OK);

  int64_t reservedByCore1[TREESIZE + 5];

  //reserve more frames than one tree
  for(int i = 0; i < TREESIZE + 5; ++i){
    reservedByCore1[i] = llc_get(upper, 1, 0);
    check(reservedByCore1[i]  > 0, "");
  }

  check_uequal(getTreeIdx(reservedByCore1[0]), getTreeIdx(reservedByCore1[TREESIZE -1]))
  check(getTreeIdx(reservedByCore1[0]) != getTreeIdx(reservedByCore1[TREESIZE]), "");
  int64_t ret2 = llc_get(upper, 2, 0);

  // free half the frames from old tree with core 2
  for(int i = 0; i < TREESIZE / 2; ++i){
    ret = llc_put(upper, 2, reservedByCore1[i], 0);
    check_uequal(ret, 0ul);
  }
  // core 2 must have now this first tree reserved
  check_uequal(upper->local[2].reserved.preferred_index, getAtomicIdx(reservedByCore1[0]))


  check(getTreeIdx( ret2) > getTreeIdx( ret), "second get must be in different tree");

  return success;
}

int llc_tests(int *test_counter, int *fail_counter) {
  run_test(init_llc_test);
  run_test(general_function_test);
  run_test(test_put);

  return 0;
}