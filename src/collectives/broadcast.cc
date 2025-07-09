#include "enqueue.h"

tcclResult_t tcclBroadcast(const void *sendbuff, void *recvbuff, size_t count, tcclDataType_t datatype, int root,
  tcclComm_t comm, sdaaStream_t stream) {
    struct tcclInfo info = { tcclCollBroadcast, "Broadcast", (void *)sendbuff, recvbuff, count, datatype, tcclSum, root, comm, stream};
    return tcclEnqueueCheck(&info);
}
