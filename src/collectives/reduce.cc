#include "comm.h"
#include "enqueue.h"

tcclResult_t tcclReduce(const void* sendbuff, void* recvbuff, size_t count,
                        tcclDataType_t datatype, tcclRedOp_t op, int root,
                        tcclComm_t comm, sdaaStream_t stream) {
  if ((count <= (1024 * 1024)) && (comm->isOneNode == true)) {
    if (root != comm->rank) {
      recvbuff = NULL;
    }
    return tcclAllReduce(sendbuff, recvbuff, count, datatype, op, comm, stream);
  }

  struct tcclInfo info = {
      tcclCollReduce, "Reduce", (void*)sendbuff, recvbuff, count, datatype, op,
      root,           comm,     stream};
  return tcclEnqueueCheck(&info);
}
