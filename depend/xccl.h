/*************************************************************************
 * Copyright (c) 2015-2019, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef XCCL_H_
#define XCCL_H_

#define XCCL_MAJOR 2
#define XCCL_MINOR 15
#define XCCL_PATCH 1
#define XCCL_SUFFIX ""

#define XCCL_VERSION_CODE 21510
#define XCCL_VERSION(X,Y,Z) (((X) <= 2 && (Y) <= 8) ? (X) * 1000 + (Y) * 100 + (Z) : (X) * 10000 + (Y) * 100 + (Z))

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to communicator */
typedef struct xcclComm* xcclComm_t;

#define XCCL_UNIQUE_ID_BYTES 128
typedef struct { char internal[XCCL_UNIQUE_ID_BYTES]; } xcclUniqueId;

/* Error type */
typedef enum { xcclSuccess                 =  0,
               xcclUnhandledCudaError      =  1,
               xcclSystemError             =  2,
               xcclInternalError           =  3,
               xcclInvalidArgument         =  4,
               xcclInvalidUsage            =  5,
               xcclRemoteError             =  6,
               xcclInProgress              =  7,
               xcclNumResults              =  8 } xcclResult_t;

/* Communicator configuration. Users can assign value to attributes to specify the
 * behavior of a communicator. */
typedef struct xcclConfig_v21400 {
  /* attributes that users should never touch. */
  int size;
  unsigned int magic;
  unsigned int version;
  /* attributes that users are able to customize. */
  int blocking;
} xcclConfig_t;

/* Config initializer must be assigned to initialize config structure when it is created.
 * Not initialized config will result in XCCL error. */
#define XCCL_CONFIG_INITIALIZER {                                       \
  sizeof(xcclConfig_t), /* size */                                      \
  0xcafebeef,           /* magic */                                     \
  XCCL_VERSION(XCCL_MAJOR, XCCL_MINOR, XCCL_PATCH), /* version */       \
  1                     /* blocking */                                  \
}

/* Return the XCCL_VERSION_CODE of the XCCL library in the supplied integer.
 * This integer is coded with the MAJOR, MINOR and PATCH level of the
 * XCCL library
 */
xcclResult_t  xcclGetVersion(int *version);
xcclResult_t pxcclGetVersion(int *version);

/* Generates an Id to be used in xcclCommInitRank. xcclGetUniqueId should be
 * called once and the Id should be distributed to all ranks in the
 * communicator before calling xcclCommInitRank. */
xcclResult_t  xcclGetUniqueId(xcclUniqueId* uniqueId);
xcclResult_t pxcclGetUniqueId(xcclUniqueId* uniqueId);

/* Create a new communicator (multi thread/process version) with a configuration
 * set by users. */
xcclResult_t  xcclCommInitRankConfig(xcclComm_t* comm, int nranks, xcclUniqueId commId, int rank, xcclConfig_t* config);
xcclResult_t pxcclCommInitRankConfig(xcclComm_t* comm, int nranks, xcclUniqueId commId, int rank, xcclConfig_t* config);

/* Creates a new communicator (multi thread/process version).
 * rank must be between 0 and nranks-1 and unique within a communicator clique.
 * Each rank is associated to a CUDA device, which has to be set before calling
 * xcclCommInitRank.
 * xcclCommInitRank implicitly syncronizes with other ranks, so it must be
 * called by different threads/processes or use xcclGroupStart/xcclGroupEnd. */
xcclResult_t  xcclCommInitRank(xcclComm_t* comm, int nranks, xcclUniqueId commId, int rank);
xcclResult_t pxcclCommInitRank(xcclComm_t* comm, int nranks, xcclUniqueId commId, int rank);

/* Creates a clique of communicators (single process version).
 * This is a convenience function to create a single-process communicator clique.
 * Returns an array of ndev newly initialized communicators in comm.
 * comm should be pre-allocated with size at least ndev*sizeof(xcclComm_t).
 * If devlist is NULL, the first ndev CUDA devices are used.
 * Order of devlist defines user-order of processors within the communicator. */
xcclResult_t  xcclCommInitAll(xcclComm_t* comm, int ndev, const int* devlist);
xcclResult_t pxcclCommInitAll(xcclComm_t* comm, int ndev, const int* devlist);

/* Finalize a communicator. xcclCommFinalize flushes all issued communications,
 * and marks communicator state as xcclInProgress. The state will change to xcclSuccess
 * when the communicator is globally quiescent and related resources are freed; then,
 * calling xcclCommDestroy can locally free the rest of the resources (e.g. communicator
 * itself) without blocking. */
xcclResult_t  xcclCommFinalize(xcclComm_t comm);
xcclResult_t pxcclCommFinalize(xcclComm_t comm);

/* Frees local resources associated with communicator object. */
xcclResult_t  xcclCommDestroy(xcclComm_t comm);
xcclResult_t pxcclCommDestroy(xcclComm_t comm);

/* Frees resources associated with communicator object and aborts any operations
 * that might still be running on the device. */
xcclResult_t  xcclCommAbort(xcclComm_t comm);
xcclResult_t pxcclCommAbort(xcclComm_t comm);

/* Returns a string for each error code. */
const char*  xcclGetErrorString(xcclResult_t result);
const char* pxcclGetErrorString(xcclResult_t result);

/* Checks whether the comm has encountered any asynchronous errors */
xcclResult_t  xcclCommGetAsyncError(xcclComm_t comm, xcclResult_t *asyncError);
xcclResult_t pxcclCommGetAsyncError(xcclComm_t comm, xcclResult_t *asyncError);

/* Gets the number of ranks in the communicator clique. */
xcclResult_t  xcclCommCount(const xcclComm_t comm, int* count);
xcclResult_t pxcclCommCount(const xcclComm_t comm, int* count);

/* Returns the cuda device number associated with the communicator. */
xcclResult_t  xcclCommCuDevice(const xcclComm_t comm, int* device);
xcclResult_t pxcclCommCuDevice(const xcclComm_t comm, int* device);

/* Returns the user-ordered "rank" associated with the communicator. */
xcclResult_t  xcclCommUserRank(const xcclComm_t comm, int* rank);
xcclResult_t pxcclCommUserRank(const xcclComm_t comm, int* rank);

/* Reduction operation selector */
typedef enum { xcclNumOps_dummy = 5 } xcclRedOp_dummy_t;
typedef enum { xcclSum        = 0,
               xcclProd       = 1,
               xcclMax        = 2,
               xcclMin        = 3,
               xcclAvg        = 4,
               /* xcclNumOps: The number of built-in xcclRedOp_t values. Also
                * serves as the least possible value for dynamic xcclRedOp_t's
                * as constructed by xcclRedOpCreate*** functions. */
               xcclNumOps     = 5,
               /* xcclMaxRedOp: The largest valid value for xcclRedOp_t.
                * It is defined to be the largest signed value (since compilers
                * are permitted to use signed enums) that won't grow
                * sizeof(xcclRedOp_t) when compared to previous XCCL versions to
                * maintain ABI compatibility. */
               xcclMaxRedOp   = 0x7fffffff>>(32-8*sizeof(xcclRedOp_dummy_t))
             } xcclRedOp_t;

/* Data types */
typedef enum { xcclInt8       = 0, xcclChar       = 0,
               xcclUint8      = 1,
               xcclInt32      = 2, xcclInt        = 2,
               xcclUint32     = 3,
               xcclInt64      = 4,
               xcclUint64     = 5,
               xcclFloat16    = 6, xcclHalf       = 6,
               xcclFloat32    = 7, xcclFloat      = 7,
               xcclFloat64    = 8, xcclDouble     = 8,
               xcclBfloat16   = 9,
               xcclNumTypes   = 10
} xcclDataType_t;

/* xcclScalarResidence_t: Location and dereferencing logic for scalar arguments. */
typedef enum {
  /* xcclScalarDevice: The scalar is in device-visible memory and will be
   * dereferenced while the collective is running. */
  xcclScalarDevice = 0,

  /* xcclScalarHostImmediate: The scalar is in host-visible memory and will be
   * dereferenced before the xcclRedOpCreate***() function returns. */
  xcclScalarHostImmediate = 1
} xcclScalarResidence_t;

/*
 * xcclRedOpCreatePreMulSum
 *
 * Creates a new reduction operator which pre-multiplies input values by a given
 * scalar locally before reducing them with peer values via summation. For use
 * only with collectives launched against *comm* and *datatype*. The
 * *residence* argument indicates how/when the memory pointed to by *scalar*
 * will be dereferenced. Upon return, the newly created operator's handle
 * is stored in *op*.
 */
xcclResult_t  xcclRedOpCreatePreMulSum(xcclRedOp_t *op, void *scalar, xcclDataType_t datatype, xcclScalarResidence_t residence, xcclComm_t comm);
xcclResult_t pxcclRedOpCreatePreMulSum(xcclRedOp_t *op, void *scalar, xcclDataType_t datatype, xcclScalarResidence_t residence, xcclComm_t comm);

/*
 * xcclRedOpDestroy
 *
 * Destroys the reduction operator *op*. The operator must have been created by
 * xcclRedOpCreatePreMul with the matching communicator *comm*. An operator may be
 * destroyed as soon as the last XCCL function which is given that operator returns.
 */
xcclResult_t xcclRedOpDestroy(xcclRedOp_t op, xcclComm_t comm);
xcclResult_t pxcclRedOpDestroy(xcclRedOp_t op, xcclComm_t comm);

/*
 * Group Start
 *
 * Start a group call. All calls to XCCL until xcclGroupEnd will be fused into
 * a single XCCL operation. Nothing will be started on the CUDA stream until
 * xcclGroupEnd.
 */
xcclResult_t  xcclGroupStart();
xcclResult_t pxcclGroupStart();

/*
 * Group End
 *
 * End a group call. Start a fused XCCL operation consisting of all calls since
 * xcclGroupStart. Operations on the CUDA stream depending on the XCCL operations
 * need to be called after xcclGroupEnd.
 */
xcclResult_t  xcclGroupEnd();
xcclResult_t pxcclGroupEnd();

#include "net.h"
#include <stdarg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define XCCLCHECK(call) do { \
  tcclResult_t res = (tcclResult_t)call; \
  if (res != tcclSuccess) { \
    /* Print the back trace*/ \
    return res; \
  } \
} while (0);

enum xcclIbCommState {
  xcclIbCommStateStart = 0,
  xcclIbCommStateConnect = 1,
  xcclIbCommStateAccept = 3,
  xcclIbCommStateSend = 4,
  xcclIbCommStateRecv = 5,
  xcclIbCommStateConnected = 6,
};

union xcclSocketAddress {
  struct sockaddr sa;
  struct sockaddr_in sin;
  struct sockaddr_in6 sin6;
};

struct xcclIbCommStage {
  enum xcclIbCommState state;
  int offset;
  void* buffer;
  void* comm;
};

struct xcclIbHandle {
  union xcclSocketAddress connectAddr; // Filled by the target
  struct xcclIbCommStage stage; // Used by the other side when connecting
};

struct xcclSharpRequest {
  int requestType;
  void *sharpRequest;
  int  size;
  int  used;
};

struct xcclSharpListenComm {
  int   dev;
  void *listenCommP2P;
};

struct xcclSharpCollComm {
  int    rank;
  int    nranks;
  void*  recvComm;
  void*  sendComm;
  struct xcclSharpRequest*   reqs;
  struct sharp_coll_context* sharpCollContext;
  struct sharp_coll_comm*    sharpCollComm;
};

int xcclSharpOobBarrier(void *ctx);
int xcclSharpOobGather(void *ctx, int root, void *sbuf, void *rbuf, int size);
int xcclSharpOobBcast(void *ctx, void *buf, int size, int root);
xcclResult_t xcclSharpListen(int dev, void* opaqueHandle, void** listenComm);
xcclResult_t xcclSharpInit(xcclDebugLogger_t logFunction);
xcclResult_t xcclSharpConnect(void* handles[], int nranks, int rank, void* listenComm, void** collComm);
xcclResult_t xcclIbMalloc(void** ptr, size_t size);
xcclResult_t xcclIbConnect(int dev, void* opaqueHandle, void** sendComm);
xcclResult_t xcclIbAccept(void* listenComm, void** recvComm);

#ifdef __cplusplus
} // end extern "C"
#endif

#endif // end include guard
