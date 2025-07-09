/*************************************************************************
 * Copyright (c) 2016-2018, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef XCCL_UTILS_H_
#define XCCL_UTILS_H_

#include "xccl.h"
#include <stdint.h>

#define XCCL_STATIC_ASSERT(_cond, _msg) \
    switch(0) {case 0:case (_cond):;}

xcclResult_t xcclIbMalloc(void** ptr, size_t size);
xcclResult_t xcclRealloc(void** ptr, size_t old_size, size_t new_size);
xcclResult_t getHostName(char* hostname, int maxlen);
uint64_t getHostHash();
uint64_t getPidHash();

struct netIf {
  char prefix[64];
  int port;
};

int parseStringList(const char* string, struct netIf* ifList, int maxList);
int matchIfList(const char* string, int port, struct netIf* ifList, int listSize, int matchExact);
int readFileNumber(long *value, const char *filename_fmt, ...);
const char *get_plugin_lib_path();

#endif
