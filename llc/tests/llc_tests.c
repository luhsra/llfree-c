#include "llc_tests.h"
#include "../llc.h"
#include "bitfield.h"
#include "check.h"

#include "../enum.h"
#include "../pfn.h"
#include "../utils.h"
#include "assert.h"
#include "lower.h"
#include "pthread.h"
#include "search.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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
  check_equal(upper->local->last_free.free_counter, 1);

  // reserve all frames in first tree
  for (int i = 0; i < TREESIZE; ++i) {
    ret = llc_get(upper, 0, 0);
    check(ret >= 0, "");
  }

  check(ret >= 0, "");

  // reserve first frame in new tree
  ret = llc_get(upper, 0, 0);
  check(ret >= 0, "");
  check(upper->local[0].reserved.preferred_index = getAtomicIdx(TREESIZE),
        "second tree must be allocated");

  size_t free_frames = llc_free_frames(upper);
  // reserve and free a HugeFrame
  ret = llc_get(upper, 0, HP);
  check(ret >= 0, "");
  check_uequal(llc_free_frames(upper), free_frames - FIELDSIZE)
      check_uequal(llc_put(upper, 0, ret, HP), 0ul /*ERR_OK*/);
  check_uequal(llc_free_frames(upper), free_frames)

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

bool test_put() {
  bool success = true;
  upper_t *upper = (upper_t *)llc_default();
  int64_t ret = llc_init(upper, 4, 0, 132000, 0, true);
  assert(ret == ERR_OK);

  int64_t reservedByCore1[TREESIZE + 5];

  // reserve more frames than one tree
  for (int i = 0; i < TREESIZE + 5; ++i) {
    reservedByCore1[i] = llc_get(upper, 1, 0);
    check(reservedByCore1[i] >= 0, "");
  }

  check_uequal(getTreeIdx(reservedByCore1[0]),
               getTreeIdx(reservedByCore1[TREESIZE - 1]))
      check(getTreeIdx(reservedByCore1[0]) !=
                getTreeIdx(reservedByCore1[TREESIZE]),
            "");
  int64_t ret2 = llc_get(upper, 2, 0);

  check(getTreeIdx(ret2) > getTreeIdx(reservedByCore1[TREESIZE]),
        "second get must be in different tree");

  // free half the frames from old tree with core 2
  for (int i = 0; i < TREESIZE / 2; ++i) {
    ret = llc_put(upper, 2, reservedByCore1[i], 0);
    check_uequal(ret, 0ul);
  }
  // core 2 must have now this first tree reserved
  check_uequal(upper->local[2].reserved.preferred_index,
               getAtomicIdx(reservedByCore1[0]))

      return success;
}

bool llc_allocAll() {
  const uint64_t MEMORYSIZE = (1ul << 30); // 8GiB
  const uint64_t LENGTH = (MEMORYSIZE / FRAME_SIZE);
  const int CORES = 8;
  int success = true;
  upper_t *upper = llc_default();
  int64_t ret = llc_init(upper, CORES, 1024, LENGTH, 0, true);
  assert(ret == ERR_OK);

  int64_t *pfns = malloc(sizeof(int64_t) * LENGTH);
  assert(pfns != NULL);

  for (size_t i = 0; i < LENGTH; ++i) {
    int64_t ret = llc_get(upper, i % CORES, 0);
    check(ret >= 0, "must be able to alloc the whole memory");
    pfns[i] = ret;
  }
  check_uequal(llc_free_frames(upper), 0ul);
  check_uequal(allocated_frames(&upper->lower), llc_frames(upper));

  return success;
}

struct arg {
  int core;
  int order;
  size_t amount;
  size_t allocations;
};
struct ret {
  int64_t *pfns;
  size_t sp;
  size_t amount_ENOMEM;
};

upper_t *upper;

static int64_t contains(int64_t *sorted_list, size_t len, int64_t item) {
  if (item < sorted_list[0] || item > sorted_list[len - 1])
    return -1;

  for (size_t i = 0; i < len; ++i) {
    if (sorted_list[i] == item)
      return i;
    if (sorted_list[i] > item)
      return -1;
  }
  return -1;
}

static int comp(const void *a, const void *b) {
  int64_t *x = (int64_t *)a;
  int64_t *y = (int64_t *)b;

  return *x - *y;
}

static void *allocFrames(void *arg) {
  struct arg *args = arg;

  struct ret *ret = malloc(sizeof(struct ret));
  assert(ret != NULL);

  ret->pfns = malloc(args->amount * sizeof(int64_t));
  assert(ret->pfns != NULL);

  srandom(args->core);
  for (size_t i = 0; i < args->allocations; ++i) {

    // if full or in 1/3 times free a random reserved frame;
    if (ret->sp == args->amount || (ret->sp > 0 && random() % 8 > 4)) {
      size_t random_idx = random() % ret->sp;
      assert(llc_put(upper, args->core, ret->pfns[random_idx], args->order) ==
             ERR_OK);
      --ret->sp;
      ret->pfns[random_idx] = ret->pfns[ret->sp];
      --i;
    } else {
      ret->pfns[ret->sp] = llc_get(upper, args->core, args->order);
      if (ret->pfns[ret->sp] == ERR_MEMORY)
        ++ret->amount_ENOMEM;
      else
        ++ret->sp;
    }
  }
  ret->pfns = realloc(ret->pfns, ret->sp * sizeof(int64_t));
  assert(ret->pfns != NULL);
  qsort(ret->pfns, ret->sp, sizeof(int64_t), comp);
  pthread_exit(ret);
  return NULL;
}
bool multithreaded_alloc() {
  int success = true;

  const int CORES = 8;
  const uint64_t LENGTH = TREESIZE * (CORES);

  upper = llc_default();
  assert(llc_init(upper, CORES, 0, LENGTH, 0, true) == ERR_OK);
  pthread_t threads[CORES];
  struct arg args[CORES];
  for (int i = 0; i < CORES; ++i) {
    args[i] = (struct arg){i, 0, (TREESIZE + 500), 40000};
    assert(pthread_create(&threads[i], NULL, allocFrames, &args[i]) == 0);
  }

  struct ret *rets[CORES];
  for (int i = 0; i < CORES; ++i) {
    assert(pthread_join(threads[i], (void **)&rets[i]) == 0);
    assert(rets[i] != NULL);
  }

  size_t still_reserved = 0;
  size_t err = 0;
  for (int i = 0; i < CORES; ++i) {
    still_reserved += rets[i]->sp;
    err += rets[i]->amount_ENOMEM;
  }
  // now all threads are terminated
  check_uequal(llc_frames(upper) - llc_free_frames(upper), still_reserved);

  // duplicate check
  if (true) {
    for (size_t core = 0; core < CORES; ++core) {
      for (size_t i = core + 1; i < CORES; ++i) {
        for (size_t idx = 0; idx < rets[core]->sp; ++idx) {
          int64_t frame = rets[core]->pfns[idx];
          int64_t same = contains(rets[i]->pfns, rets[i]->sp, frame);
          if (same >= 0) {
            success = false;
            printf("\tFound duplicate reserved Frame\n both core %lu and %lu "
                   "reserved frame %ld in tree %lu\n",
                   core, i, frame, getTreeIdx(frame));
            // leave all loops
            goto end;
          }
        }
      }
    }
  }
end:

  if (!success) {
    llc_print(upper);
    printf("%lu times ERR_MEMORY was returned\n", err);
  }
  for (size_t i = 0; i < CORES; ++i) {
    free(rets[i]->pfns);
    free(rets[i]);
  }
  llc_drop(upper);
  return success;
}

int llc_tests(int *test_counter, int *fail_counter) {
  run_test(init_llc_test);
  run_test(general_function_test);
  run_test(test_put);
  run_test(llc_allocAll);
  run_test(multithreaded_alloc);
  return 0;
}