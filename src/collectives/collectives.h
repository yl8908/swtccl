#ifndef TCCL_COLLECTIVES_H
#define TCCL_COLLECTIVES_H

#include <cstdint>
#include "comm.h"

tcclResult_t tcclSleepKernel(sdaaStream_t stream, unsigned long msec);
tcclResult_t tcclDeviceReady(sdaaStream_t stream);
template<size_t LDM_SIZE = 8 * 1024>
tcclResult_t tcclDeviceAllReduce(struct collArgs *args, GraphManager *graph_manager = nullptr);
template<size_t LDM_SIZE = 8 * 1024>
tcclResult_t tcclDeviceAllReduceOneProc(struct collArgs *args);
template<size_t LDM_SIZE = 8 * 1024>
tcclResult_t tcclDeviceReduce(struct collArgs* args);
tcclResult_t tcclDeviceAllReduceResideKernel(struct resideKernelArgs* reside_args, GraphManager *graph_manager = nullptr);
tcclResult_t tcclDeviceAlltoall(struct collArgs* args);
void host_calculate(void *in0, void *in1, void *out, size_t count, tcclDataType_t datatype, tcclRedOp_t op);
int tcclsizeof_datatype(tcclDataType_t datatype);
#endif
