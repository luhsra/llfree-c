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
      (llc_frames(arg->upper) >> HP) / (arg->upper->cores - 1);
  int64_t *PFNs = calloc(max_allocs, sizeof(*PFNs));
  if (PFNs == NULL)
    die("calloc failed");
  struct ret *ret = malloc(sizeof(struct ret));
  if (ret == NULL)
    die("malloc failed");
  uint64_t allocations = 0;
  for (; allocations < max_allocs; ++allocations) {
    PFNs[allocations] = llc_get(arg->upper, arg->core, HP);
    if (PFNs[allocations] < 0) {
      ret->failed_allocations++;
      break;
    }
  }

  for (uint64_t i = 0; i < allocations; ++i) {
    if (PFNs[i] >= 0) {
      int success = llc_put(arg->upper, arg->core, PFNs[i], HP);
      if (success < 0)
        ++ret->failed_frees;
    }
  }
  free(PFNs);
  ret->allocations = allocations;
  return ret;
}

int alloc_all_as_HP() {
  int64_t len = 1ul << 30;
  int cores = 4;
  upper_t *upper = llc_default();
  int64_t ret = llc_init(upper, cores, 0, len, VOLATILE, true);
  if (ret != ERR_OK)
    return 1;

  const int num_of_HPs = len >> HP;
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
  return 0;
}
