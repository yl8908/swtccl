#include "enqueue.h"
#include "comm.h"

tcclResult_t tcclAlltoall(const void *sendbuff, void *recvbuff, size_t count, tcclDataType_t datatype, tcclComm_t comm, sdaaStream_t stream) {
    struct tcclInfo info = { tcclCollAlltoall, "Alltoall", (void *)sendbuff, recvbuff, count, datatype, tcclNumOps, 0, comm, stream };
    if (!comm->isOneNode) {
        for (int i = 0; i < comm->nranks; i++) {
            if (tcclIsInTheNode(comm, i)) continue;
            tcclIbInitUnconnectedPeer(comm, i);
        }
    }
    return tcclEnqueueCheck(&info);
}

tcclResult_t tcclAlltoallv(const void *sendbuff, const size_t sendcounts[], const int sdispls[],
                           void *recvbuff, const size_t recvcounts[], const int rdispls[],
                           tcclDataType_t datatype, tcclComm_t comm, sdaaStream_t stream) {
    int nranks = comm->nranks;
    int rank = comm->rank;

    int start_no = nranks - 1 - rank;
    int peer;

    TCCLCHECK(tccl_move_to_cross_pro((void *)((uintptr_t)sendbuff + sdispls[rank]),
        (void *)((uintptr_t)recvbuff + rdispls[rank]), sendcounts[rank] * tcclsizeof_datatype(datatype), stream));

    for (int i = 0; i < nranks; i++) {
        peer = (start_no + i) % nranks;
        void *send = (void *)((uintptr_t)sendbuff + sdispls[peer]);
        void *recv = (void *)((uintptr_t)recvbuff + rdispls[peer]);
        if (rank < peer) {
            TCCLCHECK(tcclSend(send, sendcounts[peer], datatype, peer, comm, stream));
            TCCLCHECK(tcclRecv(recv, recvcounts[peer], datatype, peer, comm, stream));
        } else if (rank > peer) {
            TCCLCHECK(tcclRecv(recv, recvcounts[peer], datatype, peer, comm, stream));
            TCCLCHECK(tcclSend(send, sendcounts[peer], datatype, peer, comm, stream));
        } else {
        }
    }
    return tcclSuccess;
}
