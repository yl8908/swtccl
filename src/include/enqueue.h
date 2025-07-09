#ifndef TCCL_ENQUEUE_H
#define TCCL_ENQUEUE_H

#include "tccl.h"

typedef enum {
    tcclCollBroadcast,
    tcclCollAllGather,
    tcclCollReduce,
    tcclCollReduceScatter,
    tcclCollAllReduce,
    tcclCollSend,
    tcclCollRecv,
    tcclCollAlltoall
} tcclColl_t;

struct tcclInfo {
    tcclColl_t coll;
    const char *opName;
    void *sendbuf;
    void *recvbuf;
    size_t count;
    tcclDataType_t datatype;
    tcclRedOp_t op;
    int root;
    tcclComm_t comm;
    sdaaStream_t stream;
};

tcclResult_t tcclEnqueueCheck(struct tcclInfo *info);

#endif
