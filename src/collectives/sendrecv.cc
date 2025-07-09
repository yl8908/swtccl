#include "comm.h"
#include "enqueue.h"

extern std::unordered_map<IP_ADDR, IB_TABLE_ROW> IB_connect_table;

    tcclResult_t tcclSend(const void *sendbuff, size_t count, tcclDataType_t datatype, int peer, tcclComm_t comm,
                          sdaaStream_t stream) {
        struct tcclInfo info = {tcclCollSend, "Send", (void *)sendbuff, NULL, count, datatype, tcclSum, peer,
                                comm,         stream};
        tcclConnectType ctype = comm->connectType[peer];
        if (ctype == TCCL_DMA) {
            TCCLCHECK(tcclEnqueueCheck(&info));
        } else {
            if (ctype == TCCL_IB) {
              tcclIbInitUnconnectedPeer(comm, peer);
            }
            int totalcnt = info.count;
            void *sendtmp = info.sendbuf;
            int countpiece = MAX_SIZE / tcclsizeof_datatype(info.datatype);
            for (int i = 0; (i * countpiece) < totalcnt; i++) {
                if (((i + 1) * countpiece) > totalcnt) {
                    info.count = totalcnt - i * countpiece;
                } else {
                    info.count = countpiece;
                }
                info.sendbuf = (void *)((uintptr_t)sendtmp + i * MAX_SIZE);
                TCCLCHECK(tcclEnqueueCheck(&info));
            }
        }
        return tcclSuccess;
    }


tcclResult_t tcclRecv(void *recvbuff, size_t count, tcclDataType_t datatype,
                      int peer, tcclComm_t comm, sdaaStream_t stream) {
  struct tcclInfo info = {tcclCollRecv, "Recv",   NULL,    (void *)recvbuff,
                          count,        datatype, tcclSum, peer,
                          comm,         stream};
  tcclConnectType ctype = comm->connectType[peer];
  if (ctype == TCCL_DMA) {
    TCCLCHECK(tcclEnqueueCheck(&info));
  } else {
    if (ctype == TCCL_IB) {
        tcclIbInitUnconnectedPeer(comm, peer);
    }
    int totalcnt = info.count;
    void *recvtmp = info.recvbuf;
    int countpiece = MAX_SIZE / tcclsizeof_datatype(info.datatype);
    for (int i = 0; (i * countpiece) < totalcnt; i++) {
      if (((i + 1) * countpiece) > totalcnt) {
        info.count = totalcnt - i * countpiece;
      } else {
        info.count = countpiece;
      }
      info.recvbuf = (void *)((uintptr_t)recvtmp + i * MAX_SIZE);
      TCCLCHECK(tcclEnqueueCheck(&info));
    }
  }
  return tcclSuccess;
}
