// Copyright (c) 2023 Tecorigin Co., Ltd. All rights reserved.
//
// NOTICE TO LICENSE:
// This source code and/or documentation ("Licensed Deliverables") are subject
// to TECORIGIN intellectual property rights under CHINA and
// international Copyright laws.
//
// These Licensed Deliverables contained herein is PROPRIETARY and CONFIDENTIAL
// to TECORIGIN and is being provided under the terms and conditions of a
// form of TECORIGIN software license agreement by and between TECORIGIN and
// Licensee ("License Agreement") or electronically accepted by Licensee.
//
// Notwithstanding any terms or conditions to the contrary in the License
// Agreement, reproduction or disclosure of the Licensed Deliverables to any
// third party without the express written consent of TECORIGIN is prohibited.
//
// NOTWITHSTANDING ANY TERMS OR CONDITIONS TO THE CONTRARY IN THE LICENSE
// AGREEMENT, TECORIGIN MAKES NO REPRESENTATION ABOUT THE SUITABILITY OF THESE
// LICENSED DELIVERABLES FOR ANY PURPOSE.  IT IS PROVIDED "AS IS" WITHOUT
// EXPRESS OR IMPLIED WARRANTY OF ANY KIND. TECORIGIN DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THESE LICENSED DELIVERABLES, INCLUDING ALL IMPLIED WARRANTIES
// OF MERCHANTABILITY,NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE.
//
// NOTWITHSTANDING ANY TERMS OR CONDITIONS TO THE CONTRARY IN THE LICENSE
// AGREEMENT, IN NO EVENT SHALL TECORIGIN BE LIABLE FOR ANY SPECIAL, INDIRECT,
// INCIDENTAL, OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING
// FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH
// THE USE OR PERFORMANCE OF THESE LICENSED DELIVERABLES.

#ifndef TCCL_H
#define TCCL_H

//#include </data/sdaaruntime/sdaa/runtime/ocelot/sdaa/interface/sdaa_runtime.h>
#include <sdaa_runtime.h>   //by-yl

#ifdef __cplusplus
extern "C" {
#endif

#define TCCL_VERSION_CODE 1200
#define TCCL_SPLIT_NOCOLOR (-1)

typedef struct tcclComm* tcclComm_t;

typedef struct {
    char internal[128];
} tcclUniqueId;

typedef enum {
    tcclSuccess            =  0,
    tcclUnhandledSdaaError =  1,
    tcclSystemError        =  2,
    tcclInternalError      =  3,
    tcclInvalidArgument    =  4,
    tcclInvalidUsage       =  5,
    tcclInProgress         =  6,
    tcclNumResults         =  7
} tcclResult_t;

typedef struct {
  int param1;
  int param2;
} tcclConfig_t;

tcclResult_t tcclGetUniqueId(tcclUniqueId *uniqueId);
tcclResult_t tcclCommInitRank(tcclComm_t *comm, int nranks, tcclUniqueId commIdRef, int rank);
tcclResult_t tcclCommDestroy(tcclComm_t comm);
tcclResult_t tcclCommSplit(tcclComm_t comm, int color, int key, tcclComm_t *newcomm, tcclConfig_t* config);
tcclResult_t tcclCommCount(const tcclComm_t comm, int *count);
tcclResult_t tcclCommDevice(const tcclComm_t comm, int *device);
tcclResult_t tcclCommUserRank(const tcclComm_t comm, int *rank);
tcclResult_t tcclCommInitAll(tcclComm_t* comms, int ndev, const int* devlist);
tcclResult_t tcclCommInitRankConfig(tcclComm_t* comm, int nranks, tcclUniqueId commId, int rank, tcclConfig_t* config);
tcclResult_t tcclCommFinalize(tcclComm_t comm);
const char* tcclGetLastError(tcclComm_t comm);
tcclResult_t tcclCommGetAsyncError(tcclComm_t comm, tcclResult_t *asyncError);

const char *tcclGetErrorString(tcclResult_t result);

typedef enum {
    tcclSum    = 0,
    tcclProd   = 1,
    tcclMax    = 2,
    tcclMin    = 3,
    tcclNumOps = 4
} tcclRedOp_t;

typedef enum {
    tcclInt8     = 0, tcclChar   = 0,
    tcclUint8    = 1,
    tcclInt32    = 2, tcclInt    = 2,
    tcclUint32   = 3, tcclUint   = 3,
    tcclFloat32  = 4, tcclFloat  = 4,
    tcclFloat64  = 5, tcclDouble = 5,
    tcclInt64    = 6,
    tcclUint64   = 7,
    tcclFloat16  = 8, tcclHalf   = 8,
    tcclBF16     = 9,
    tcclNumTypes = 10
} tcclDataType_t;

tcclResult_t tcclBroadcast(const void *sendbuff, void *recvbuff, size_t count, tcclDataType_t datatype, int root, tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclAllReduce(const void *sendbuff, void *recvbuff, size_t count, tcclDataType_t datatype, tcclRedOp_t op, tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclReduce(const void* sendbuff, void* recvbuff, size_t count, tcclDataType_t datatype, tcclRedOp_t op, int root, tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclReduceScatter(const void *sendbuff, void *recvbuff, size_t recvcount, tcclDataType_t datatype, tcclRedOp_t op, tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclAllGather(const void *sendbuff, void *recvbuff, size_t count, tcclDataType_t datatype, tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclAlltoall(const void *sendbuff, void *recvbuff, size_t count, tcclDataType_t datatype, tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclAlltoallv(const void *sendbuff, const size_t sendcounts[], const int sdispls[], void *recvbuff, const size_t recvcounts[],
    const int rdispls[], tcclDataType_t datatype, tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclSend(const void *sendbuff, size_t count, tcclDataType_t datatype, int peer, tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclRecv(void *recvbuff, size_t count, tcclDataType_t datatype, int peer, tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclGetVersion(int *version);
#ifdef __cplusplus
}
#endif // __cplusplus

#endif // TCCL_H
