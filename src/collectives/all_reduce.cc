#include "enqueue.h"

tcclResult_t tcclAllReduce(const void *sendbuff, void *recvbuff, size_t count, tcclDataType_t datatype, tcclRedOp_t op, tcclComm_t comm, sdaaStream_t stream) {
    struct tcclInfo info = { tcclCollAllReduce, "AllReduce", (void *)sendbuff, recvbuff, count, datatype, op, 0, comm, stream};
    return tcclEnqueueCheck(&info);
}
