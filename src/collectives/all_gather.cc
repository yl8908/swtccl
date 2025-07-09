#include "collectives.h"
#include "comm.h"
#include "enqueue.h"

tcclResult_t tcclAllGather(const void *sendbuff, void *recvbuff, size_t count, tcclDataType_t datatype, tcclComm_t comm,
                           sdaaStream_t stream) {
    long long data_size = (long long)count * tcclsizeof_datatype(datatype);
    long long total_data_size = (long long)data_size * comm->nranks;

    if ((data_size % ALIGNED_BYTE != 0)) {
        void *recvHost = malloc(total_data_size);
        void *recvTmp = malloc(data_size);
        if ((recvHost == NULL) || (recvTmp == NULL)) {
            if (recvHost) free(recvHost);
            if (recvTmp) free(recvTmp);
            return tcclSystemError;
        }

        for (int i = 0; i < comm->nranks; i++) {
            void *recvHost_d = (void *)((uintptr_t)recvHost + i * data_size);
            tcclBroadcast(sendbuff, recvbuff, count, datatype, i, comm, stream);
            SDAACHECK(sdaaStreamSynchronize(stream));
            SDAACHECK(sdaaMemcpyAsync(recvTmp, recvbuff, data_size, sdaaMemcpyDeviceToHost, stream));
            SDAACHECK(sdaaStreamSynchronize(stream));
            memcpy(recvHost_d, recvTmp, data_size);
            TCCLCHECK(tcclBarrierAll(comm));
        }
        SDAACHECK(sdaaMemcpyAsync(recvbuff, recvHost, total_data_size, sdaaMemcpyHostToDevice, stream));
        SDAACHECK(sdaaStreamSynchronize(stream));
        free(recvTmp);
        free(recvHost);
        return tcclSuccess;
    }

    struct tcclInfo info = {
        tcclCollAllGather, "AllGather", (void *)sendbuff, recvbuff, count, datatype, tcclNumOps, 0, comm, stream};
    return tcclEnqueueCheck(&info);
}
