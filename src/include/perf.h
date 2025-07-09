#ifndef TCCL_PERF
#define TCCL_PERF

#include <sys/time.h>

#ifdef _PERF_TEST_
#define TEST_PRINTF printf
#define TEST_GET_TIME(dot) \
  struct timeval dot;      \
  gettimeofday(&dot, NULL);

#define TEST_CAL_TIME(time, start, end)                                      \
  double time =                                                              \
      (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec); \
  time = time / 1000000;

#else
#define TEST_PRINTF
#define TEST_GET_TIME(dot)
#define TEST_CAL_TIME(time, start, end) double time = 0;
#endif

#endif
