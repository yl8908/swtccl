/*************************************************************************
 * Copyright (c) 2017-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef XCCL_PARAM_H_
#define XCCL_PARAM_H_

#include <stdint.h>

const char* userHomeDir();
void setEnvFile(const char* fileName);
void initEnv();

void xcclLoadParam(char const* env, int64_t deftVal, int64_t uninitialized, int64_t* cache);

#define XCCL_PARAM(name, env, deftVal) \
  int64_t xcclParam##name() { \
    XCCL_STATIC_ASSERT(deftVal != INT64_MIN, "default value cannot be the uninitialized value."); \
    static int64_t cache = INT64_MIN; \
    if (__builtin_expect(__atomic_load_n(&cache, __ATOMIC_RELAXED) == INT64_MIN, false)) { \
      xcclLoadParam("XCCL_" env, deftVal, INT64_MIN, &cache); \
  } \
    return cache; \
  }

#endif
