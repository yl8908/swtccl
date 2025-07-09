/*************************************************************************
 * Copyright (c) 2015-2018, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef XCCL_CORE_H_
#define XCCL_CORE_H_

#include "xccl.h"
#include "debug.h"

#include <stdint.h>
#include <stdlib.h>

#define MIN(a, b) ((a)<(b)?(a):(b))
#define MAX(a, b) ((a)>(b)?(a):(b))

#define DIVUP(x, y) \
    (((x)+(y)-1)/(y))
#define ROUNDUP(x, y) \
    (DIVUP((x), (y))*(y))

#include <errno.h>
// Check system calls
#define SYSCHECK(call, name) do { \
  int retval; \
  SYSCHECKVAL(call, name, retval); \
} while (0)

#define SYSCHECKVAL(call, name, retval) do { \
  SYSCHECKSYNC(call, name, retval); \
  if (retval == -1) { \
    WARN("Call to " name " failed : %s", strerror(errno)); \
    return xcclSystemError; \
  } \
} while (0);

#define SYSCHECKSYNC(call, name, retval) do { \
  retval = call; \
  if (retval == -1 && (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)) { \
    INFO(XCCL_ALL,"Call to " name " returned %s, retrying", strerror(errno)); \
  } else { \
    break; \
  } \
} while(0)

// Propagate errors up
#define XCCLCHECK(call) do { \
  xcclResult_t res = call; \
  if (res != xcclSuccess) { \
    /* Print the back trace*/ \
    INFO(XCCL_ALL,"%s:%d -> %d", __FILE__, __LINE__, res);    \
    return res; \
  } \
} while (0);

#define XCCLCHECKGOTO(call, res, label) do { \
  res = call; \
  if (res != xcclSuccess) { \
    /* Print the back trace*/ \
    INFO(XCCL_ALL,"%s:%d -> %d", __FILE__, __LINE__, res);    \
    goto label; \
  } \
} while (0);

#define NEQCHECK(statement, value) do {   \
  if ((statement) != value) {             \
    /* Print the back trace*/             \
    INFO(XCCL_ALL,"%s:%d -> %d", __FILE__, __LINE__, xcclSystemError);    \
    return xcclSystemError;     \
  }                             \
} while (0);

#define NEQCHECKGOTO(statement, value, res, label) do { \
  if ((statement) != value) { \
    /* Print the back trace*/ \
    res = xcclSystemError;    \
    INFO(XCCL_ALL,"%s:%d -> %d", __FILE__, __LINE__, res);    \
    goto label; \
  } \
} while (0);

#define EQCHECK(statement, value) do {    \
  if ((statement) == value) {             \
    /* Print the back trace*/             \
    INFO(XCCL_ALL,"%s:%d -> %d", __FILE__, __LINE__, xcclSystemError);    \
    return xcclSystemError;     \
  }                             \
} while (0);

#define EQCHECKGOTO(statement, value, res, label) do { \
  if ((statement) == value) { \
    /* Print the back trace*/ \
    res = xcclSystemError;    \
    INFO(XCCL_ALL,"%s:%d -> %d", __FILE__, __LINE__, res);    \
    goto label; \
  } \
} while (0);



#endif // end include guard
