/*************************************************************************
 * Copyright (c) 2015-2018, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef XCCL_DEBUG_H_
#define XCCL_DEBUG_H_

#include "core.h"

#include <stdio.h>

#include <sys/syscall.h>
#include <limits.h>
#include <string.h>
#include <pthread.h>
#include "net.h"

// Conform to pthread and NVTX standard
#define XCCL_THREAD_NAMELEN 16


extern xcclDebugLogger_t pluginLogFunction;

#define WARN(...) pluginLogFunction(XCCL_LOG_WARN, XCCL_ALL, __FILE__, __LINE__, __VA_ARGS__)
#define INFO(FLAGS, ...) pluginLogFunction(XCCL_LOG_INFO, (FLAGS), __func__, __LINE__, __VA_ARGS__)

#ifdef ENABLE_TRACE
#define TRACE(FLAGS, ...) pluginLogFunction(XCCL_LOG_TRACE, (FLAGS), __func__, __LINE__, __VA_ARGS__)
#else
#define TRACE(...)
#endif

void xcclSetThreadName(pthread_t thread, const char *fmt, ...);

#endif
