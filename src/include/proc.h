#ifndef TCCL_PROC_H
#define TCCL_PROC_H

#include "enqueue.h"
#include "ib.h"
#include "sharp.h"
#include "tccl.h"
#include "yangl.h" // by-yl
#define MAX_CARD_NUM 8
#define DEV_NUM tcclGetDevNum()
#define DEV_NUM_MAX 4
// reserve for split flag
#define SPLIT_FLAG_MAX 20

#define MAX_DEV_NUM (MAX_CARD_NUM * DEV_NUM)

typedef enum {
    SEND_BUFF  = 0,
    RECV_BUFF  = 1,
    BUFF_BUTT  = 2,
} BUFF_TYPE;

typedef enum {
    CAL_DEFAULT = 0,  // 默认算法,分段计算
    CAL_ALL = 1,      // 小数据全量算法,降低延时,要求四字节对齐
    CAL_ASSUME_ALIGNED_NM = 2,  // 正常数据量大小，假设为四字节对齐，函数内部判断非则修改为默认算法。
    CAL_TYPE_BUTT
} CAL_TYPE;

struct tcclIb {
    ibvResources send;  // use send buff
    ibvResources recv;  // use recv buff
};

struct tcclNode {
    int rankList[DEV_NUM_MAX];
    int rank_id;
    int rank_num;
    int card_id;
    int card_num;
    int node_id;
    int node_num;
    int port;
    int devid;

    bool sharpEnable;
    struct tcclSocket *sharpNext;
    struct tcclSocket *sharpPrev;
    char ibv_name[16];
};

struct tcclRankAddrMap {
    int root_devid;
    int root_rank;
    char *inBuff;
    char *outBuff;

    sdaaIpcMemHandle_t inHandle;
    sdaaIpcMemHandle_t outHandle;
};

struct tcclCardAddrMap {
    uint64_t pid; /* process id */
    struct tcclRankAddrMap rankMap[DEV_NUM_MAX];
    char *smallBuff;
    sdaaIpcMemHandle_t smallHandle;
};

struct tcclIbRingArgs {
    tcclComm_t comm;
    BUFF_TYPE type;
    size_t datasize;
    int off;
    int peer;
    int flagoff;
};

#define TCCL_IB_LAUNCH_HOST(comm_, type_, sizeBytes_, off_, peer_, flagoff_) do {  \
    struct tcclIbRingArgs *args_ = (struct tcclIbRingArgs *)malloc(sizeof(struct tcclIbRingArgs));  \
    if (args_ != NULL) {  \
        (args_)->comm = (comm_);  \
        (args_)->type = (type_);  \
        (args_)->datasize = (sizeBytes_);  \
        (args_)->off = (off_);  \
        (args_)->peer = (peer_);  \
        (args_)->flagoff = (flagoff_);  \
        SDAACHECK(tcclLaunchHostFunc(stream, tcclPostIbFunc, (args_)));  \
    } else {  \
        printf("ib launch host mem malloc failed\n");  \
    }  \
} while (0)

struct tcclTransportParam {
    struct tcclIbRingArgs ibArgs;
    // used in tccl_move_to_cross
    bool readDeviceRecv;
    int reserved1;
    int reserved2;
};

tcclResult_t tcclPublicWork(tcclComm_t comm, struct collArgs *args);
tcclResult_t tcclNicPublicWork(tcclComm_t comm, struct collArgs *args);
tcclResult_t tcclPublicInit(tcclComm_t comm);
tcclResult_t tcclPublicFini(tcclComm_t comm);
tcclResult_t tcclCrossBuffInit(tcclComm_t comm);
tcclResult_t tcclReuseMemAllocMap(char **in, char **in_host, char **out, char **out_host, int devid);
bool tcclIsInTheCard(int rankList[], int rank);
bool tcclIsInTheNode(tcclComm_t comm, int peer_rank);
void tcclGetPeerId(tcclComm_t comm, int peer_rank, int *peer_card_id, int *peer_rank_id);
int tcclGetDevNum(void);
tcclResult_t tcclRankSendPost(int off, BUFF_TYPE type, size_t sizeBytes, int peer, tcclComm_t comm,
                              sdaaStream_t stream);
tcclResult_t tcclRankRecvPost(int off, BUFF_TYPE type, size_t sizeBytes, int peer, tcclComm_t comm,
                              sdaaStream_t stream);
tcclResult_t tcclRankSendPostNoWait(int off, BUFF_TYPE type, size_t sizeBytes, int peer, tcclComm_t comm,
                                    sdaaStream_t stream, bool isSendFlag = true);
tcclResult_t tcclRankRecvPostNoWait(int off, BUFF_TYPE type, size_t sizeBytes, int peer, tcclComm_t comm,
                                    sdaaStream_t stream);
tcclResult_t tcclRankSendFlag(int no, int flag, int peer, tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclRankWaitFlag(int no, int flag, tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclBcastSendAckFlag(int no, int value, tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclBcastWaitAckFlag(int no, int stepNo, int nChunks, tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclTransportSend(tcclComm_t comm, int peer, void* src_addr, void* dst_addr, size_t data_size, sdaaStream_t stream, struct tcclTransportParam *extra = NULL);
void tcclPostIbFunc(void *args);

#endif
