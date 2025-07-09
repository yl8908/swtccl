#include "collectives.h"
#include "comm.h"
#include "enqueue.h"

tcclResult_t tcclReduceScatter(const void *sendbuff, void *recvbuff,
                               size_t recvcount, tcclDataType_t datatype,
                               tcclRedOp_t op, tcclComm_t comm,
                               sdaaStream_t stream) {
  struct tcclInfo info = {tcclCollReduceScatter,
                          "ReduceScatter",
                          (void *)sendbuff,
                          recvbuff,
                          recvcount,
                          datatype,
                          op,
                          0,
                          comm,
                          stream};
  return tcclEnqueueCheck(&info);
}
