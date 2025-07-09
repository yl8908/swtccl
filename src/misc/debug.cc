#include "debug.h"
#include "comm.h"
#include <stdarg.h>

#define TCCL_TRACE_PATH_LEN 128
#define TCCL_TRACE_FILE_MAX (2*1024*1024)
int file_max = TCCL_TRACE_FILE_MAX;

#define FPRINTF(format, args...) do {  \
    if (comm->fp) {  \
        if (ftell(comm->fp) >= file_max) {  \
            ftruncate(fileno(comm->fp), ftell(comm->fp) + 1);  \
            fseek(comm->fp, 0, SEEK_SET);  \
        }  \
        fprintf(comm->fp, format, ##args); \
    }  \
} while (0)

FILE *g_traceFile[MAX_CARD_NUM * DEV_NUM_MAX] = { 0 };
char tcclLastError[1024] = "";
pthread_mutex_t tcclDebugLock = PTHREAD_MUTEX_INITIALIZER;

tcclResult_t tcclTraceLoginit(tcclComm_t comm) {
    int devid = comm->devid;

    comm->fp = NULL;

    char *path = getenv("TCCL_TRACE_LOG_PATH");
    if ((path == NULL) || (strlen(path) > TCCL_TRACE_PATH_LEN)) {
        return tcclSuccess;
    }

    if (getenv("TCCL_TRACE_FILE_MAX")) file_max = atoi(getenv("TCCL_TRACE_FILE_MAX"));

    if (g_traceFile[devid] == NULL) {
        char filename[TCCL_TRACE_PATH_LEN*2] = { 0 };
        struct tcclIP *IP = (struct tcclIP*)&comm->proxy->dev[comm->rank].ipaddr;;
        (void)snprintf(filename, sizeof(filename), "%stccl_trace_log_%d.%d.%d.%d_%d", path, IP->a, IP->b, IP->c, IP->d, devid);
        g_traceFile[devid] = fopen(filename, "w+");
        if (g_traceFile[devid] == NULL) {
            TCCL_ERROR_LOG("fopen filename %s error %d(%s)", filename, errno, strerror(errno));
        }
    }
    comm->fp = g_traceFile[devid];
    return tcclSuccess;
}

sdaaError_t tcclTraceStreamWriteValue64(sdaaStream_t stream, void *ptr, uint64_t value, unsigned int flags, tcclComm_t comm, const char *func, int line) {
    FPRINTF("%s:%d Write, stream:%p, addr:%p, val:%ld, flag:%d, port:%d,%d\n", func, line, stream, ptr, value, flags, comm->port, comm->optcnt);
    return sdaaStreamWriteValue64(stream, ptr, value, flags);
}

sdaaError_t tcclTraceStreamWaitValue64(sdaaStream_t stream, void *ptr, uint64_t value, unsigned int flags, tcclComm_t comm, const char *func, int line) {
    FPRINTF("%s:%d Wait(rank:%d), stream:%p, addr:%p, val:%ld, flag:%d, port:%d,%d\n", func, line, comm->rank, stream, ptr, value, flags, comm->port, comm->optcnt);
    return sdaaStreamWaitValue64(stream, ptr, value, flags);
}

sdaaError_t tcclTraceIpcMemcpyPeertoPeerAsync(void* dst, const void* src, size_t sizeBytes, int relaxOrderFlag, sdaaStream_t stream, tcclComm_t comm, const char *func, int line) {
    FPRINTF("%s:%d P2P rank(%d->%d), dst:%p, src:%p, size:%ld, flag:%d, port:%d,%d\n", func, line, comm->rank, comm->allRing->phyNextRank, dst, src, sizeBytes, relaxOrderFlag, comm->port, comm->optcnt);
    return sdaaIpcMemcpyPeertoPeerAsync(dst, src, sizeBytes, relaxOrderFlag, stream);
}

sdaaError_t tcclTraceLaunchHostFunc(sdaaStream_t stream, sdaaHostFn_t fn, void *userData, tcclComm_t comm, const char *func, int line) {
    FPRINTF("%s:%d Host(rank:%d), stream:%p, fn:%p, userData:%p, port:%d,%d\n", func, line, comm->rank, stream, fn, userData, comm->port, comm->optcnt);
    return sdaaLaunchHostFunc(stream, fn, userData);
}

sdaaError_t tcclTraceNicRdmaWriteAsync(sdaaStream_t stream, sdaaNicRdmaQueue_t hNicRdmaQueue, uint64_t remote_guid,
                                       uint64_t src_addr, uint64_t dst_addr, size_t size, tcclComm_t comm,
                                       const char *func, int line) {
    FPRINTF("%s:%d Rdma, stream:%p, queue:%p, remote_guid:%ld, src:%ld, dst:%ld, size:%ld, port:%d,%d\n",
                func, line, stream, hNicRdmaQueue, remote_guid, src_addr, dst_addr, size, comm->port, comm->optcnt);
    return sdaaNicRdmaWriteAsync(stream, hNicRdmaQueue, remote_guid, src_addr, dst_addr, size);
}

void tcclSetLastError(const char *fmt, ...) {
    pthread_mutex_lock(&tcclDebugLock);
    va_list vargs;
    va_start(vargs, fmt);
    (void) vsnprintf(tcclLastError, sizeof(tcclLastError), fmt, vargs);
    printf("%s", tcclLastError);
    va_end(vargs);
    pthread_mutex_unlock(&tcclDebugLock);
}
