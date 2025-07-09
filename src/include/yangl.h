/*******************************************************************************
 *                                                                              *
 *         WARNING! WARNING!! WARNING!!!                                        *
 *         you are using experimental APIs which are not intended               *
 *         for use in production environments!                                  *
 *                                                                              *
 *******************************************************************************/

#ifndef INCLUDE_YANGL_H_
#define INCLUDE_YANGL_H_


#include <assert.h>
#include <stdlib.h>

#include "tccl.h"

#ifdef __cplusplus
#include <array>
#include <iosfwd>
#include <type_traits>
#endif  //__cplusplus
	//
	//
#define SDAA_LAUNCH_PARAM_BUFFER_POINTER ((void*)0x01)
#define SDAA_LAUNCH_PARAM_BUFFER_SIZE ((void*)0x02)
#define SDAA_LAUNCH_PARAM_END ((void*)0x00)

#define SDAACHECK(error)                                                                           \
  {                                                                                                \
    sdaaError_t localError = error;                                                                \
    if ((localError != sdaaSuccess)) {                                                             \
      printf("%serror: '%s'(%d) from %s at %s:%d%s\n", KRED, sdaaGetErrorString(localError),       \
             localError, #error, __FUNCTION__, __LINE__, KNRM);                                    \
      exit(EXIT_FAILURE);                                                                          \
    }                                                                                              \
  }

//#define checkSdaaErrors SDAACHECK



#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"
#define KGRN "\x1B[32m"
#define KYEL "\x1B[33m"
#define KBLU "\x1B[34m"
#define KMAG "\x1B[35m"
#define KCYN "\x1B[36m"
#define KWHT "\x1B[37m"

typedef enum { PE_MODE, COL_MODE = 2 } dma_mode;

typedef enum sdaaStreamOpType {
  SDAA_STREAM_MEM_OP_WAIT_VALUE64 = 0x0,
  SDAA_STREAM_MEM_OP_SET_VALUE64 = 0x1,
} sdaaStreamOpType;

typedef enum SDAAstreamWaitValue_flags_enum {
  SDAA_STREAM_WAIT_VALUE_GEQ = 0x0, /* Wait until (uint64_t)(*addr - value) >= 0. */
  SDAA_STREAM_WAIT_VALUE_EQ = 0x1,  /* Wait until *addr == value. */
} sdaaStreamWaitValue_flags;

typedef enum SDAAstreamWriteValue_flags_enum {
  SDAA_STREAM_WRITE_VALUE_DEFAULT = 0x0, /* *addr = value */
  SDAA_STREAM_WRITE_VALUE_ADD = 0x1,     /* *addr = *addr + value */
} sdaaStreamWriteValue_flags;

typedef enum SDAAIpcMemcpy_flags_enum {
  SDAA_IPC_P2P_RO_CLOSE = 0x0,
  SDAA_IPC_P2P_RO_OPEN = 0x1,
} sdaaIpcMemcpy_flags;

typedef struct sdaaIpcMemHandle_st {
  char reserved[64];
} sdaaIpcMemHandle_t;


/**
 * SDAA GPU kernel node parameters
 */
struct sdaaKernelNodeParams {
  void* func;          /**< Kernel to launch */
  void** kernelParams; /**< Array of pointers to individual kernel arguments*/
  void** extra;        /**< Pointer to kernel arguments in the "extra" format */
};

/**
 * SDAA P2P Copy node parameters
 */
struct sdaaP2pcpyNodeParams {
  void* src_vaddr;
  void* dst_vaddr;
  size_t ByteCount;
  int RelaxOrderFlag;
};

/**
 * SDAA Memory Ops node parameters
 */
struct sdaaMemOpNodeParams {
  sdaaStreamOpType type;
  void* addr;
  uint64_t value;
  unsigned int flags;
};


typedef struct sdaaMemOpNodeParams sdaaStreamBatchMemOpParams;

typedef struct SDnicRdmaQueue_st* sdaaNicRdmaQueue_t;

typedef struct isdaaModule_t* sdaaModule_t;

typedef struct isdaaModuleSymbol_t* sdaaFunction_t;

typedef struct SDgraph_st* sdaaGraph_t;

typedef struct SDgraphNode_st* sdaaGraphNode_t;

typedef struct SDgraphExec_st* sdaaGraphExec_t;


typedef void (*sdaaHostFn_t)(void* userData);
sdaaError_t sdaaLaunchHostFunc(sdaaStream_t stream, sdaaHostFn_t fn, void* userData);
sdaaError_t sdaaStreamWriteValue64(sdaaStream_t stream, void* ptr, uint64_t value, unsigned int flags);
sdaaError_t sdaaStreamWaitValue64(sdaaStream_t stream, void* ptr, uint64_t value, unsigned int flags);
sdaaError_t sdaaMemDeviceGetHostPointer(void** pp, void* dptr);
sdaaError_t sdaaMemDevicePutHostPointer(void* ptr);
sdaaError_t sdaaMemcpyPeer(void* dst, int dstDevice, const void* src, int srcDevice,
                           size_t sizeBytes);
sdaaError_t sdaaNicRdmaWriteAsync(sdaaStream_t stream, sdaaNicRdmaQueue_t hNicRdmaQueue,
                                  uint64_t remote_guid, uint64_t src_addr, uint64_t dst_addr,
                                  size_t size);
sdaaError_t sdaaNicRdmaQueueCreate(sdaaNicRdmaQueue_t* phNicRdmaQueue, uint64_t nic_bar_addr,
                                   uint64_t depth);
sdaaError_t sdaaIpcMemcpyPeertoPeerAsync(void* dst, const void* src, size_t sizeBytes,
                                         int relaxOrderFlag, sdaaStream_t stream);
sdaaError_t sdaaCrossMemToBusMem(void* cross_ptr, uint64_t* busMem);

sdaaError_t sdaaStreamBatchMemOp(sdaaStream_t stream, unsigned int count,
                                 sdaaStreamBatchMemOpParams* paramArray, unsigned int flags);
sdaaError_t sdaaIpcGetMemHandle(sdaaIpcMemHandle_t* handle, void* devPtr);
sdaaError_t sdaaGraphCreate(sdaaGraph_t* pGraph, unsigned int flags);
sdaaError_t sdaaIpcOpenMemHandle(void** devPtr, sdaaIpcMemHandle_t handle, unsigned int flags);
sdaaError_t sdaaGraphDestroy(sdaaGraph_t graph);

sdaaError_t sdaaGraphAddKernelNode(sdaaGraphNode_t* pGraphNode, sdaaGraph_t graph,
                                   const sdaaGraphNode_t* pDependencies, size_t numDependencies,
                                   const struct sdaaKernelNodeParams* pNodeParams);
//这个函数判断为函数名错误,暂时与sdaaGraphAddKernelNode一致
sdaaError_t sdaaGraphAddResideKernelNode(sdaaGraphNode_t* pGraphNode, sdaaGraph_t graph,
                                   const sdaaGraphNode_t* pDependencies, size_t numDependencies,
                                   const struct sdaaKernelNodeParams* pNodeParams);


sdaaError_t sdaaGraphAddP2pcpyNode(sdaaGraphNode_t* pGraphNode, sdaaGraph_t graph,
                                   const sdaaGraphNode_t* pDependencies, size_t numDependencies,
                                   const struct sdaaP2pcpyNodeParams* pCopyParams);

sdaaError_t sdaaGraphAddMemOpNode(sdaaGraphNode_t* pGraphNode, sdaaGraph_t graph,
                                  const sdaaGraphNode_t* pDependencies, size_t numDependencies,
                                  const struct sdaaMemOpNodeParams* pMemopParams);
sdaaError_t sdaaGraphInstantiate(sdaaGraphExec_t* pGraphExec, sdaaGraph_t graph,
                                 unsigned long long flags);

sdaaError_t sdaaGraphExecDestroy(sdaaGraphExec_t graphExec);

sdaaError_t sdaaGraphExecKernelNodeSetParams(sdaaGraphExec_t hGraphExec, sdaaGraphNode_t node,
                                             const struct sdaaKernelNodeParams* pNodeParams);

sdaaError_t sdaaGraphExecP2pcpyNodeSetParams(sdaaGraphExec_t hGraphExec, sdaaGraphNode_t node,
                                             const struct sdaaP2pcpyNodeParams* pCopyParams);

sdaaError_t sdaaGraphExecMemOpNodeSetParams(sdaaGraphExec_t hGraphExec, sdaaGraphNode_t node,
                                            const struct sdaaMemOpNodeParams* pMemopParams);

sdaaError_t sdaaGraphLaunch(sdaaGraphExec_t graphExec, sdaaStream_t stream);

sdaaError_t sdaaMallocCross(void** ptr, size_t size);
//extern sdaaError_t sdaaDeviceGetAttribute(int* value, sdaaDeviceAttr attr, int device);
#endif
