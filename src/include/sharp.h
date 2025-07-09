#ifndef TCCL_SHARP_H
#define TCCL_SHARP_H

#include "debug.h"
#include "tccl.h"
#include "yangl.h" // by-yl
typedef enum tcclSharpStatus {
    TCCL_SHARP_INIT = 1,
    TCCL_SHARP_WORK = 2,
    TCCL_SHARP_COMM = 3,
    TCCL_SHARP_FREE = 4,
    TCCL_SHARP_BUTT
} STATUS;

struct tcclSharpPub {
    int devid;
    int initflag;

    char *hostSend;
    char *deviSend;
    void *s_mem_mr;
    sdaaIpcMemHandle_t handleSend;
};

struct tcclSharpCtl {
    volatile STATUS status;
    bool isrootnode;
    int root_devid;
    int commport;
    int comm_id;
    int rank_no;
    struct tcclSharpPub map[32];
};

struct tcclSharpMap {
    volatile STATUS status;
    int comm_id;
    int comm_port;
    int rank_no;
    int total_rank_num;
    int devid;
    int rank_id;
    int card_id;

    size_t count;
    tcclDataType_t datatype;
    tcclRedOp_t op;
    int offset;
};

struct tcclSharpArgs {
    tcclComm_t comm;
    int offset;
    int datasize;
    bool isrootnode;
    size_t count;
    tcclDataType_t datatype;
    tcclRedOp_t op;
};

struct tcclInfoList {
    int comm_port;
    int rank_no;
};

tcclResult_t tcclSharpPre(struct tcclNode *node);
tcclResult_t tcclSharpInit(struct tcclNode *node);
tcclResult_t tcclSharpFini(struct tcclNode *node);
tcclResult_t tcclSharpIbvNameGet(struct tcclNode *node);
tcclResult_t tcclGetSharpAddr(tcclComm_t comm);
void tcclAllreduceInterFunc(void *args);

#endif
