#include "bitfield.h"
#include "enum.h"
#include "llc.h"
#include "lower.h"
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int alloc_all_as_HP();
int rand_reg();

#define die(msg)                                                               \
  do {                                                                         \
    printf("Die in %s:%d: %s", __FILE__, __LINE__, msg);                       \
    exit(1);                                                                   \
  } while (0)

int main(int argc, char **argv) {
  char *benches[] = {"allHP_MC", "rand"};
  bool correct = false;
  if (argc == 2) {
    for (unsigned i = 0; i < sizeof(benches) / sizeof(*benches); ++i) {
      if (strcmp(argv[1], benches[i]) == 0) {
        correct = true;
        break;
      }
    }
  }
  if (argc != 2 || !correct) {
    printf("please secify witch benchmark to run\n");
    for (unsigned i = 0; i < sizeof(benches) / sizeof(*benches); ++i) {
      printf("%s\n", benches[i]);
    }
    exit(1);
  }

  srand(1337);
  if (!strcmp(argv[1], benches[0])) {
    int ret = alloc_all_as_HP();
    if (ret != 0)
      printf("alloc_all_as_HP returnd a faliure\n");
  }
  if (!strcmp(argv[1], benches[1])) {
    int ret = rand_reg();
    if (ret != 0)
      printf("rand returnd a faliure\n");
  }
  return 0;
}

struct arg {
  upper_t *upper;
  unsigned core;
  unsigned allocations;
  int64_t *list;
  unsigned len;
  struct random_data *r_data;
};
struct ret {
  unsigned failed_allocations;
  unsigned failed_frees;
  unsigned allocations;
};

void *allocHPs(void *a) {
  struct arg *arg = (struct arg *)a;
  unsigned max_allocs =
      (llc_frames(arg->upper) >> HP_ORDER) / (arg->upper->cores - 1);
  int64_t *PFNs = calloc(max_allocs, sizeof(*PFNs));
  if (PFNs == NULL)
    die("calloc failed");
  struct ret *ret = malloc(sizeof(struct ret));
  if (ret == NULL)
    die("malloc failed");
  uint64_t allocations = 0;
  for (; allocations < max_allocs; ++allocations) {
    PFNs[allocations] = llc_get(arg->upper, arg->core, HP_ORDER);
    if (PFNs[allocations] < 0) {
      ret->failed_allocations++;
      break;
    }
  }

  for (uint64_t i = 0; i < allocations; ++i) {
    if (PFNs[i] >= 0) {
      int success = llc_put(arg->upper, arg->core, PFNs[i], HP_ORDER);
      if (success < 0)
        ++ret->failed_frees;
    }
  }
  free(PFNs);
  ret->allocations = allocations;
  return ret;
}

int alloc_all_as_HP() {
  int64_t len = 1ul << 22;
  int cores = 4;
  upper_t *upper = llc_default();
  char* memory = aligned_alloc(PAGESIZE, len * PAGESIZE);
  if(memory == NULL) die("malloc memory");

  int64_t ret = llc_init(upper, cores, (uint64_t) memory, len, OVERWRITE, true);
  if (ret != ERR_OK)
    return 1;

  const int num_of_HPs = len >> HP_ORDER;
  struct arg args[cores];
  pthread_t threads[cores];
  for (int i = 0; i < cores; ++i) {
    args[i] = (struct arg){upper, i, num_of_HPs / cores, NULL, 0, NULL};
    int ret = pthread_create(&threads[i], NULL, allocHPs, &args[i]);
    if (ret != 0)
      die("pthread create");
  }

  struct ret *rets[cores];
  unsigned failed_allocations = 0;
  unsigned failed_frees = 0;
  unsigned allocations = 0;
  for (int i = 0; i < cores; ++i) {
    if (pthread_join(threads[i], (void **)&rets[i]) != 0)
      die("pthread_join failed");
    failed_allocations += rets[i]->failed_allocations;
    failed_frees += rets[i]->failed_frees;
    allocations += rets[i]->allocations;
  }

  llc_drop(upper);

  printf("%u out of %d Huge Frames were allocated\n%u allocation "
         "faliures\n%u free faliures\nSummary:\n",
         allocations, num_of_HPs, failed_allocations, failed_frees);
  for (int i = 0; i < cores; ++i) {
    printf("core %d: %u allocations; %u faliures\n", i, rets[i]->allocations,
           rets[i]->failed_allocations);
    free(rets[i]);
  }

  free(memory);
  return 0;
}

void shuffle(int64_t *arr, const unsigned len, struct random_data *r_data) {
  for (unsigned i = 0; i < len; ++i) {
    int32_t rand_val;
    if (r_data == NULL) {
      rand_val = rand();
    } else {
      int ret = random_r(r_data, &rand_val);
      if (ret != 0)
        die("random_r");
    }
    unsigned idx = rand_val % len;
    int64_t h = arr[i];
    arr[i] = arr[idx];
    arr[idx] = h;
  }
}

// frees 10% of given memoy and reserves 10%frames
void *allocRand(void *a) {
  struct arg *arg = (struct arg *)a;
  struct ret *ret = calloc(1, sizeof(struct ret));

  shuffle(arg->list, arg->len, arg->r_data);
  // free 10% of allokated frames
  for (unsigned i = 0; i < arg->len / 10; ++i) {
    int val = llc_put(arg->upper, arg->core, arg->list[i], 0);
    if (val < 0)
      ++ret->failed_frees;
  }
  // reserve 10% of frames
  for (unsigned i = 0; i < arg->len / 10; ++i) {
    ++ret->allocations;
    arg->list[i] = llc_get(arg->upper, arg->core, 0);
    if (arg->list[i] < 0)
      ++ret->failed_allocations;
  }
  return ret;
}

int rand_reg() {
  int64_t len = 1ul << 20;
  unsigned cores = 8;
  upper_t *upper = llc_default();
  int64_t ret = llc_init(upper, cores, 0, len, VOLATILE, true);
  if (ret != ERR_OK)
    return 1;
  const unsigned allocations = len * 0.90;
  int64_t *PFNs = calloc(allocations, sizeof(*PFNs));
  if (PFNs == 0)
    die("calloc");

  // allocate memory
  for (unsigned i = 0; i < allocations; ++i) {
    PFNs[i] = llc_get(upper, 0, 0);
    assert(PFNs[i] >= 0);
    if (PFNs[i] < 0)
      return 1;
  }
  shuffle(PFNs, allocations, NULL);
  // free a random half
  for (unsigned i = 0; i < allocations / 2; ++i) {
    int ret = llc_put(upper, 0, PFNs[i], 0);
    assert(ret == ERR_OK);
    if (ret < 0)
      return 1;
  }

  printf("After random frees are %lu out of %lu HPs are free\n",
         lower_free_HPs(&upper->lower), len >> HP_ORDER);

    struct arg args[cores];
    pthread_t threads[cores];
    struct random_data r_data[cores];
    char* statebuf = malloc(cores * 128);

    unsigned list_len = allocations / 2 / cores;
    for (unsigned i = 0; i < cores; ++i) {
      r_data[i].state = NULL;
      int ret = initstate_r(i, &statebuf[128 * i], 128, &r_data[i]);
      if (ret != 0)
        die("initstate_t");
      int64_t *list = PFNs + allocations / 2 + list_len * i;
      args[i] = (struct arg){upper, i, 0, list, list_len, &r_data[i]};
    }

  for (unsigned i = 0; i < 100; ++i) {
    // start worker to randomly free and allokate frames
    for (unsigned i = 0; i < cores; ++i) {
      int ret = pthread_create(&threads[i], NULL, allocRand, &args[i]);
      if (ret != 0)
        die("pthread create");
    }
    // join worker
    struct ret *rets[cores];
    // unsigned failed_allocations = 0;
    // unsigned failed_frees = 0;
    // unsigned allocation_attempts = 0;
    for (unsigned i = 0; i < cores; ++i) {
      if (pthread_join(threads[i], (void **)&rets[i]) != 0)
        die("pthread_join failed");
      // failed_allocations += rets[i]->failed_allocations;
      // failed_frees += rets[i]->failed_frees;
      // allocation_attempts += rets[i]->allocations;
      free(rets[i]);
    }
    // printf("worker did %u allocations\nfalied frees: %u\nfalied allocs:
    // %u\n",
    //        allocation_attempts, failed_frees, failed_allocations);
    printf("%lu out of %lu HPs are free\n", lower_free_HPs(&upper->lower),
           len >> HP_ORDER);
  }
  free(statebuf);
  llc_print(upper);
  llc_drop(upper);
  return 0;
}
