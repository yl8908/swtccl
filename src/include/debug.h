#ifndef TCCL_DEBUG_H
#define TCCL_DEBUG_H

#include "tccl.h"
#include "yangl.h"//by-yl
#define tcclStreamWriteValue64(stream, ptr, value, flags) tcclTraceStreamWriteValue64(stream, ptr, value, flags, comm, __FUNCTION__, __LINE__)
#define tcclStreamWaitValue64(stream, ptr, value, flags) tcclTraceStreamWaitValue64(stream, ptr, value, flags, comm, __FUNCTION__, __LINE__)
#define tcclIpcMemcpyPeertoPeerAsync(dst, src, sizeBytes, relaxOrderFlag, stream) tcclTraceIpcMemcpyPeertoPeerAsync(dst, src, sizeBytes, relaxOrderFlag, stream, comm, __FUNCTION__, __LINE__)
#define tcclLaunchHostFunc(stream, fn, userData) tcclTraceLaunchHostFunc(stream, fn, userData, comm, __FUNCTION__, __LINE__)
#define tcclNicRdmaWriteAsync(stream, hNicRdmaQueue, remote_guid, src_addr, dst_addr, size) tcclTraceNicRdmaWriteAsync(stream, hNicRdmaQueue, remote_guid, src_addr, dst_addr, size, comm, __FUNCTION__, __LINE__)

extern char tcclLastError[];
extern pthread_mutex_t tcclDebugLock;
void tcclSetLastError(const char *fmt, ...);

#define WARN(...)
#define INFO(...)

tcclResult_t tcclTraceLoginit(tcclComm_t comm);

sdaaError_t tcclTraceStreamWriteValue64(sdaaStream_t stream, void *ptr, uint64_t value, unsigned int flags, tcclComm_t comm, const char *func, int line);
sdaaError_t tcclTraceStreamWaitValue64(sdaaStream_t stream, void *ptr, uint64_t value, unsigned int flags, tcclComm_t comm, const char *func, int line);
sdaaError_t tcclTraceIpcMemcpyPeertoPeerAsync(void* dst, const void* src, size_t sizeBytes, int relaxOrderFlag, sdaaStream_t stream, tcclComm_t comm, const char *func, int line);
sdaaError_t tcclTraceLaunchHostFunc(sdaaStream_t stream, sdaaHostFn_t fn, void *userData, tcclComm_t comm, const char *func, int line);
sdaaError_t tcclTraceNicRdmaWriteAsync(sdaaStream_t stream, sdaaNicRdmaQueue_t hNicRdmaQueue, uint64_t remote_guid,
                                       uint64_t src_addr, uint64_t dst_addr, size_t size, tcclComm_t comm,
                                       const char *func, int line);
#endif
