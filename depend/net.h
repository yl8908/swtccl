/*
 * Copyright (c) 2017-2022, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef XCCL_NET_H_
#define XCCL_NET_H_

#include <stdint.h>
#include <stdlib.h>

#define XCCL_NET_HANDLE_MAXSIZE 128

#define XCCL_PTR_HOST 0x1
#define XCCL_PTR_CUDA 0x2
#define XCCL_PTR_DMABUF 0x4

// Maximum number of requests per comm object
#define XCCL_NET_MAX_REQUESTS 8

typedef enum {XCCL_LOG_NONE=0, XCCL_LOG_VERSION=1, XCCL_LOG_WARN=2, XCCL_LOG_INFO=3, XCCL_LOG_ABORT=4, XCCL_LOG_TRACE=5} xcclDebugLogLevel;
typedef enum {XCCL_INIT=1, XCCL_COLL=2, XCCL_P2P=4, XCCL_SHM=8, XCCL_NET=16, XCCL_GRAPH=32, XCCL_TUNING=64, XCCL_ENV=128, XCCL_ALLOC=256, XCCL_CALL=512, XCCL_ALL=~0} xcclDebugLogSubSys;

typedef void (*xcclDebugLogger_t)(xcclDebugLogLevel level, unsigned long flags, const char *file, int line, const char *fmt, ...);

#include "net_v6.h"
#include "net_v5.h"
#include "net_v4.h"

#define XCCL_PLUGIN_SYMBOL xcclNetPlugin_v6

#endif // end include guard
