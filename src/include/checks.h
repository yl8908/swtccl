#ifndef TCCL_CHECKS_H
#define TCCL_CHECKS_H

#include <errno.h>
#include <stdio.h>

#define EQCHECK(statement, value) \
  do {                            \
    if ((statement) == value) {   \
      /* Print the back trace*/   \
      return tcclSystemError;     \
    }                             \
  } while (0);

#define SYSCHECK(call, name)         \
  do {                               \
    int retval;                      \
    SYSCHECKVAL(call, name, retval); \
  } while (false)

#define SYSCHECKVAL(call, name, retval)                          \
  do {                                                           \
    SYSCHECKSYNC(call, name, retval);                            \
    if (retval == -1) {                                          \
      printf("Call to " name " failed : %s\n", strerror(errno)); \
      return tcclSystemError;                                    \
    }                                                            \
  } while (false)

#define SYSCHECKSYNC(call, name, retval)                               \
  do {                                                                 \
    retval = call;                                                     \
    if (retval == -1 &&                                                \
        (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)) { \
    } else {                                                           \
      break;                                                           \
    }                                                                  \
  } while (true)

// Propagate errors up
#define TCCLCHECK(call)                                              \
  do {                                                               \
    tcclResult_t res = call;                                         \
    if (res != tcclSuccess) {                                        \
      /* Print the back trace*/                                      \
      printf("fault is %s in function %s in line %d in file %s\n",   \
             tcclGetErrorString(res), __func__, __LINE__, __FILE__); \
      return res;                                                    \
    }                                                                \
  } while (0);

#define TCCLCHECKGOTO(call)                                          \
  do {                                                               \
    tcclResult_t res = call;                                         \
    if (res != tcclSuccess) {                                        \
      /* Print the back trace*/                                      \
      printf("fault is %s in function %s in line %d in file %s\n",   \
             tcclGetErrorString(res), __func__, __LINE__, __FILE__); \
      /*INFO(TCCL_ALL,"%s:%d -> %d", __FILE__, __LINE__, res);*/     \
      goto out;                                                      \
    }                                                                \
  } while (0);

#endif
