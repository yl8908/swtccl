#include <assert.h>
#include <stdlib.h>
#include "sdaa_runtime.h"
#include <stdio.h>
#include "yangl.h"
//#include <sdaa_internel_yl.hpp>

sdaaError_t sdaaMemDeviceGetHostPointer(void** pp, void* dptr) {
  printf("sdaaMemDeviceGetHostPointer is empty");
  sdaaError_t sdaaSuccess;
}

sdaaError_t sdaaMemDevicePutHostPointer(void* ptr) {
  printf("sdaaMemDevicePutHostPointer is empty");
  sdaaError_t sdaaSuccess;
}

sdaaError_t sdaaMemcpyPeer(void* dst, int dstDevice, const void* src, int srcDevice, size_t sizeBytes)
{
  printf("sdaaMemcpyPeer is empty");
  sdaaError_t sdaaSuccess;
}

sdaaError_t sdaaNicRdmaWriteAsync(sdaaStream_t stream, sdaaNicRdmaQueue_t hNicRdmaQueue,
                                  uint64_t remote_guid, uint64_t src_addr, uint64_t dst_addr,
                                  size_t size) {
  printf("sdaaNicRdmaWriteAsync is empty");
  sdaaError_t sdaaSuccess;
}


sdaaError_t sdaaNicRdmaQueueCreate(sdaaNicRdmaQueue_t* phNicRdmaQueue, uint64_t nic_bar_addr,
                                   uint64_t depth) {
  printf("sdaaNicRdmaQueueCreate is empty");
  sdaaError_t sdaaSuccess;
}

sdaaError_t sdaaIpcMemcpyPeertoPeerAsync(void* dst, const void* src, size_t sizeBytes,
                                         int RelaxOrderFlag, sdaaStream_t stream) {
  printf("sdaaIpcMemcpyPeertoPeerAsync is empty");
  sdaaError_t sdaaSuccess;
}

sdaaError_t sdaaCrossMemToBusMem(void* cross_ptr, uint64_t* busMem) {
  printf("sdaaCrossMemToBusMem is empty");
  sdaaError_t sdaaSuccess;
}

sdaaError_t sdaaStreamBatchMemOp(sdaaStream_t stream, unsigned int count, 
		                 sdaaStreamBatchMemOpParams* paramArray, unsigned int flags){
  printf("sdaaStreamBatchMemOp is empty");
  sdaaError_t sdaaSuccess;
}


sdaaError_t sdaaIpcGetMemHandle(sdaaIpcMemHandle_t* handle, void* devPtr) {
//  SDAA_INIT_API(sdaaIpcGetMemHandle, handle, devPtr);
 
  printf("sdaaIpcGetMemHandle is empty");
  sdaaError_t sdaaSuccess;
}

sdaaError_t sdaaIpcOpenMemHandle(void** devPtr, sdaaIpcMemHandle_t handle, unsigned int flags) {
//  SDAA_INIT_API(sdaaIpcOpenMemHandle, devPtr, handle, flags);

  printf("sdaaIpcOpenMemHandle is empty");
  sdaaError_t sdaaSuccess;
}

sdaaError_t sdaaGraphCreate(sdaaGraph_t* pGraph, unsigned int flags) {
  printf("sdaaGraphCreate is empty");
  sdaaError_t sdaaSuccess;
}


sdaaError_t sdaaGraphDestroy(sdaaGraph_t graph) {
  printf("sdaaGraphDestroy is empty");
  sdaaError_t sdaaSuccess;
}


sdaaError_t sdaaGraphAddKernelNode(sdaaGraphNode_t* pGraphNode, sdaaGraph_t graph,
                                   const sdaaGraphNode_t* pDependencies, size_t numDependencies,
                                   const struct sdaaKernelNodeParams* pNodeParams) {
  printf("sdaaGraphAddKernelNode is empty");
  sdaaError_t sdaaSuccess;
}
//这个函数留意下
sdaaError_t sdaaGraphAddResideKernelNode(sdaaGraphNode_t* pGraphNode, sdaaGraph_t graph,
                                   const sdaaGraphNode_t* pDependencies, size_t numDependencies,
                                   const struct sdaaKernelNodeParams* pNodeParams) {
  printf("sdaaGraphAddResideKernelNode is empty");
  sdaaError_t sdaaSuccess;
}

sdaaError_t sdaaGraphAddP2pcpyNode(sdaaGraphNode_t* pGraphNode, sdaaGraph_t graph,
                                   const sdaaGraphNode_t* pDependencies, size_t numDependencies,
                                   const struct sdaaP2pcpyNodeParams* pCopyParams) {
  printf("sdaaGraphAddP2pcpyNode is empty");
  sdaaError_t sdaaSuccess;
}


sdaaError_t sdaaGraphAddMemOpNode(sdaaGraphNode_t* pGraphNode, sdaaGraph_t graph,
                                  const sdaaGraphNode_t* pDependencies, size_t numDependencies,
                                  const struct sdaaMemOpNodeParams* pMemopParams) {
  printf("sdaaGraphAddMemOpNode is empty");
  sdaaError_t sdaaSuccess;
}


sdaaError_t sdaaGraphInstantiate(sdaaGraphExec_t* pGraphExec, sdaaGraph_t graph,
                                 unsigned long long flags) {
  printf("sdaaGraphInstantiate is empty");
  sdaaError_t sdaaSuccess;
}


sdaaError_t sdaaGraphExecDestroy(sdaaGraphExec_t graphExec) {
  printf("sdaaGraphExecDestroy is empty");
  sdaaError_t sdaaSuccess;
}


sdaaError_t sdaaGraphExecKernelNodeSetParams(sdaaGraphExec_t hGraphExec, sdaaGraphNode_t node,
                                             const struct sdaaKernelNodeParams* pNodeParams) {
  printf("sdaaGraphExecKernelNodeSetParams is empty");
  sdaaError_t sdaaSuccess;
}


sdaaError_t sdaaGraphExecP2pcpyNodeSetParams(sdaaGraphExec_t hGraphExec, sdaaGraphNode_t node,
                                             const struct sdaaP2pcpyNodeParams* pCopyParams) {
  printf("sdaaGraphExecP2pcpyNodeSetParams is empty");
  sdaaError_t sdaaSuccess;
}


sdaaError_t sdaaGraphExecMemOpNodeSetParams(sdaaGraphExec_t hGraphExec, sdaaGraphNode_t node,
                                            const struct sdaaMemOpNodeParams* pMemopParams) {
  printf("sdaaGraphExecMemOpNodeSetParams is empty");
  sdaaError_t sdaaSuccess;
}


sdaaError_t sdaaGraphLaunch(sdaaGraphExec_t graphExec, sdaaStream_t stream) {
  printf("sdaaGraphLaunch is empty");
  sdaaError_t sdaaSuccess;
}


sdaaError_t sdaaMallocCross(void** ptr, size_t size) {
  printf("sdaaMallocCross is empty");
  sdaaError_t sdaaSuccess;
}


sdaaError_t sdaaLaunchHostFunc(sdaaStream_t stream, sdaaHostFn_t fn, void* userData){
//  SDAA_INIT_API(sdaaLaunchHostFunc, stream, fn, userData);
  printf("sdaaLaunchHostFunc is empty");
  sdaaError_t sdaaSuccess;
}

sdaaError_t sdaaStreamWriteValue64(sdaaStream_t stream, void* ptr, uint64_t value,
                                   unsigned int flags) {
//  SDAA_INIT_API(sdaaStreamWriteValue64, stream, ptr, value, flags);
  printf("sdaaStreamWriteValue64 is empty");
  sdaaError_t sdaaSuccess;
}

sdaaError_t sdaaStreamWaitValue64(sdaaStream_t stream, void* ptr, uint64_t value,
                                  unsigned int flags) {
//  SDAA_INIT_API(sdaaStreamWaitValue64, stream, ptr, value, flags);
  printf("sdaaStreamWaitValue64 is empty");
  sdaaError_t sdaaSuccess;
}

/*sdaaError_t sdaaDeviceGetAttribute(int* value, sdaaDeviceAttr attr, int device) {
  printf("sdaaDeviceGetAttribute is empty");
  sdaaError_t sdaaSuccess;
}*/
