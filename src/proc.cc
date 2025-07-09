#include "proc.h"

#include <math.h>

#include <chrono>
#include <iostream>

#include "collectives.h"
#include "debug.h"
#include "nic.h"
#include "perf.h"
#include "shm.h"
#include "unistd.h"

#define TMP_LEN (MAX_SIZE) /* 3卡场景下尾端p2p拷贝,预留1K防止溢出 */
#define MAX_DEV 32
#define PRINTF
#define FLOAT_LDM_SIZE (8 * 1024)
#define HLAF_LDM_SIZE (16 * 1024)
#define FP16_DISPATCH_SIZE (32 * 1024 * 1024)
#define BF16_DISPATCH_SIZE (16 * 1024 * 1024)
#define FP32_DISPATCH_SIZE (4 * 1024 * 1024)

tcclResult_t tcclNicStepLen(size_t *slice, int data_size, int nranks, int rank_num);
pthread_mutex_t g_memMutex = PTHREAD_MUTEX_INITIALIZER;
int g_memReuseCnt[MAX_DEV] = {0};
char *g_inCross[MAX_DEV] = {0};
char *g_inCross_host[MAX_DEV] = {0};
char *g_outCross[MAX_DEV] = {0};
char *g_outCross_host[MAX_DEV] = {0};

tcclResult_t tcclReuseMemAllocMap(char **in, char **in_host, char **out, char **out_host, int devid) {
    pthread_mutex_lock(&g_memMutex);
    if (g_memReuseCnt[devid] == 0) {
        SDAACHECK(sdaaMallocCross((void **)&g_inCross[devid], TMP_LEN));
        SDAACHECK(sdaaMemDeviceGetHostPointer((void **)&g_inCross_host[devid], g_inCross[devid]));
        SDAACHECK(sdaaMallocCross((void **)&g_outCross[devid], TMP_LEN));
        SDAACHECK(sdaaMemDeviceGetHostPointer((void **)&g_outCross_host[devid], g_outCross[devid]));

        SDAACHECK(sdaaMemset(g_inCross[devid], 0, TMP_LEN));
        SDAACHECK(sdaaMemset(g_outCross[devid], 0, TMP_LEN));
    }
    g_memReuseCnt[devid]++;
    pthread_mutex_unlock(&g_memMutex);

    *in = g_inCross[devid];
    *in_host = g_inCross_host[devid];
    *out = g_outCross[devid];
    *out_host = g_outCross_host[devid];
    return tcclSuccess;
}

tcclResult_t tcclReuseMemFree(int devid) {
    pthread_mutex_lock(&g_memMutex);
    //g_memReuseCnt[devid]--;
    //if (g_memReuseCnt[devid] == 0) {
    SDAACHECK(sdaaMemset(g_inCross[devid], 0, TMP_LEN));
    SDAACHECK(sdaaMemset(g_outCross[devid], 0, TMP_LEN));
    //SDAACHECK(sdaaFree(g_inCross[devid]));
    //SDAACHECK(sdaaFree(g_outCross[devid]));
    //SDAACHECK(sdaaMemDevicePutHostPointer(g_inCross_host[devid]));
    //SDAACHECK(sdaaMemDevicePutHostPointer(g_outCross_host[devid]));
    //g_inCross[devid] = NULL;
    //g_outCross[devid] = NULL;
    //g_inCross_host[devid] = NULL;
    //g_outCross_host[devid] = NULL;
    //}
    pthread_mutex_unlock(&g_memMutex);
    return tcclSuccess;
}

inline int tcclGetStepLen(int data_size, int nranks) {
    int slice_len = MAX_SIZE / 2 / nranks;
    slice_len = slice_len / ALIGNED_BYTE * ALIGNED_BYTE;
    if (nranks <= 32 && data_size < (128 * 1024 * 1024)) {
        //* 总数据 / 总核组数
        int data_size_per_rank = data_size / nranks;

        //* 向上取整
        if (data_size_per_rank <= 128 * 1024) {
            return (128 * 1024);
        } else if (data_size_per_rank <= 256 * 1024) {
            return (256 * 1024);
        } else if (data_size_per_rank <= 512 * 1024) {
            return (512 * 1024);
        } else if (data_size_per_rank <= 640 * 1024) {
            return (640 * 1024);
        } else if (data_size_per_rank <= 768 * 1024) {
            return (768 * 1024);
        } else if (data_size_per_rank <= 1024 * 1024) {
            return (1024 * 1024);
        } else if (data_size_per_rank <= 1536 * 1024) {
            return (1536 * 1024);
        } else if (data_size_per_rank <= 2 * 1024 * 1024) {
            return (2 * 1024 * 1024);
        } else if (data_size_per_rank <= slice_len) {
            return slice_len;
        }
    }
    return slice_len;
}

inline int tcclGetBroadcastChunkSize(size_t data_size, int rank_num) {
    size_t max_chunk_size = 4 * 1024 * 1024;
    size_t chunk_size = data_size / rank_num;

    if (chunk_size <= 128 * 1024) {
        return (128 * 1024);
    } else if (chunk_size <= 256 * 1024) {
        return (256 * 1024);
    } else if (chunk_size <= 512 * 1024) {
        return (512 * 1024);
    } else if (chunk_size <= 640 * 1024) {
        return (640 * 1024);
    } else if (chunk_size <= 768 * 1024) {
        return (768 * 1024);
    } else if (chunk_size <= 1024 * 1024) {
        return (1024 * 1024);
    } else if (chunk_size <= 1536 * 1024) {
        return (1536 * 1024);
    } else if (chunk_size <= 2 * 1024 * 1024) {
        return (2 * 1024 * 1024);
    }

    return max_chunk_size;
}

inline int lastPowerOfTwo(int num) {
    const int tmp = num;
    if (0 == num--) {
        return 1;
    }

    num = (num >> 1) | num;
    num = (num >> 2) | num;
    num = (num >> 4) | num;
    num = (num >> 8) | num;
    num = (num >> 16) | num;
    // num = (num >> 32) | num;
    ++num;
    if (tmp < num) {
        num = num >> 1;
    }
    return num;
}
template<size_t LDM_SIZE = 8 * 1024>
inline tcclResult_t tcclDeviceCalculate(char *in0, char *in1, char *out, size_t count, size_t size, tcclRedOp_t op,
                                        tcclDataType_t datatype, sdaaStream_t stream) {
    char *sendList[2] = {in0, in1};
    char *recvList[1] = {out};

    struct collArgs arg;
    arg.sendnum = 2;
    arg.recvnum = 1;
    arg.count = count;
    arg.data_size = size;
    arg.op = op;
    arg.datatype = datatype;
    arg.sendList = (void **)sendList;
    arg.recvList = (void **)recvList;
    arg.stream = stream;
    tcclDeviceAllReduceOneProc<LDM_SIZE>(&arg);
    return tcclSuccess;
}

#define CROSS_SLCE_NUM 2

tcclResult_t allReduceOnenodeA2A(tcclComm_t comm, struct collArgs *args) {
    struct tcclNode *node = comm->node;
    int rank_id = node->rank_id;
    int rank_num = node->rank_num;
    int card_id = node->card_id;
    int card_num = node->card_num;
    int node_num = node->node_num;
    const int rank = comm->rank;
    const int typelen = tcclsizeof_datatype(args->datatype);
    sdaaStream_t stream = args->stream;
    char *sendusr = args->sendusr;

    // 4 card, send signal to others cards
    for (int i = 1; i < card_num; i++) tcclDevSyncToPrevCard(comm, stream);

    int data_size = args->data_size;
    int nranks = card_num * rank_num;
    size_t step_len = tcclGetStepLen(data_size, nranks);
    // step_len = 8 * 1024 * 1024;
    // printf("steplen is: %ld\n", step_len);
    size_t step_count = step_len / typelen * card_num;
    size_t total_count = args->count;
    size_t cntslice = step_count / card_num;
    size_t sizeslice = cntslice * typelen;

    bool ifSplit = true;
    if (rank_num * step_count >= total_count) {
        ifSplit = false;
    }

    uint64_t sc = comm->sync.splitCnt;
    comm->sync.splitCnt++;
    uint64_t *sf = &comm->sync.splitFlag[sc % DEV_NUM_MAX];

    uint64_t tmpAddr;
    if (comm->node->card_num == 1) {
        tmpAddr = (uint64_t)comm->cardMap[card_id].rankMap[rank_id].outBuff;
    } else {
        tmpAddr = (uint64_t)comm->cardMap[card_id].rankMap[rank_id].inBuff;
    }
    SDAACHECK(tcclStreamWriteValue64(stream, comm->tmp, tmpAddr, SDAA_STREAM_WRITE_VALUE_DEFAULT));

    // reset split flag
    if (ifSplit && rank_id == 0) {
        SDAACHECK(tcclStreamWriteValue64(stream, sf, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT));
    }

    for (size_t cnt = rank_id * step_count; cnt < total_count; cnt += (rank_num * step_count)) {
        size_t total_offset = cnt * typelen;
        size_t off = (comm->step_no % CROSS_SLCE_NUM) * (MAX_SIZE / CROSS_SLCE_NUM);
        // printf("rank, %d, step: %d\n", rank, comm->step_no);

        char *cross_in = comm->cardMap[card_id].rankMap[rank_id].inBuff + off;
        char *cross_out = comm->cardMap[card_id].rankMap[rank_id].outBuff + off;

        int s_off = card_id * sizeslice;
        size_t cpy_cnt = step_count;
        if (cpy_cnt + cnt > total_count) cpy_cnt = total_count - cnt;

        // compute every card cpy cnt
        size_t peer_cpy_cnts[card_num] = {0};
        for (int ci = 0; ci < card_num; ci++) {
            size_t tmpcnt = (ci + 1) * cntslice < cpy_cnt ? cntslice
                            : ci * cntslice < cpy_cnt     ? cpy_cnt - ci * cntslice
                                                          : 0;
            peer_cpy_cnts[ci] = tmpcnt;
        }
        const size_t card_cpy_cnt = peer_cpy_cnts[card_id];

        // dev allrduce
        if (comm->dev_num != 1) {
            args->count = cpy_cnt;
            args->data_size = cpy_cnt * typelen;
            args->s_offset = total_offset;
            args->r_offset = off;
            args->dev_num = 1;
            args->recvList = comm->tmp;  // is cardMap inbuff
            args->caltype = CAL_ASSUME_ALIGNED_NM;
            args->useCrossBuff = false;
            if (ifSplit) SDAACHECK(tcclStreamWaitValue64(stream, sf, rank_id, SDAA_STREAM_WAIT_VALUE_EQ));
            tcclDeviceAllReduce(args);
            if (ifSplit) SDAACHECK(tcclStreamWriteValue64(stream, sf, (rank_id + 1) % rank_num, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        } else {
            tccl_move_to_cross(args->sendusr + total_offset, cross_in, cpy_cnt * typelen, stream);
        }

        // send data to other cards
        uint64_t flag = ++comm->sync.a2acnt;
        void *cntflag = comm->sync.cntflag;
        // write value to the recv card;
        SDAACHECK(tcclStreamWriteValue64(stream, cntflag, flag, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        for (int ci = 1; ci < card_num; ci++) {
            int peer = (card_id + ci) % card_num;
            size_t peer_cpy_cnt = peer_cpy_cnts[peer];
            char *send = cross_in + peer * sizeslice;
            /* when send to other card, use cross_out.*/
            char *next_recv = comm->cardMap[peer].rankMap[rank_id].outBuff + off + s_off;
            SDAACHECK(
                tcclIpcMemcpyPeertoPeerAsync(next_recv, send, peer_cpy_cnt * typelen, SDAA_IPC_P2P_RO_OPEN, stream));
            SDAACHECK(tcclIpcMemcpyPeertoPeerAsync(&comm->sync.a2alist[peer][card_id * DEV_NUM_MAX + rank_id], cntflag,
                                                   sizeof(uint64_t), SDAA_IPC_P2P_RO_CLOSE, stream));
        }
        // wait other card send done
        for (int ci = 1; ci < card_num; ci++) {
            if (card_cpy_cnt == 0) continue;
            int peer = (card_id + ci) % card_num;
            SDAACHECK(tcclStreamWaitValue64(stream, &comm->sync.a2alist[card_id][peer * DEV_NUM_MAX + rank_id], flag,
                                            SDAA_STREAM_WAIT_VALUE_GEQ));
        }

        // all reduce one time and write to local cross_in
        char *sendList[MAX_CARD_NUM];
        char *recvList[1];
        /* cross_out hold other card data + cross_in hold own card data.*/
        for (int i = 0; i < card_num; i++) {
            if (i == card_id) continue;
            sendList[i] = reinterpret_cast<char *>((uintptr_t)cross_out + i * sizeslice);
        }
        sendList[card_id] = reinterpret_cast<char *>((uintptr_t)cross_in + card_id * sizeslice);

        recvList[0] = cross_in + s_off;

        struct collArgs arg;
        memset(&arg, 0, sizeof(struct collArgs));
        arg.sendList = (void **)sendList;
        arg.sendnum = card_num;
        arg.recvList = (void **)recvList;
        arg.recvnum = 1;
        arg.data_size = card_cpy_cnt * typelen;
        arg.count = card_cpy_cnt;
        arg.op = args->op;
        arg.datatype = args->datatype;
        arg.stream = stream;
        arg.useCrossBuff = true;
        arg.caltype = CAL_ASSUME_ALIGNED_NM;
        arg.dev_num = 1;
        // tcclDeviceAllReduceOneProc(&arg);
        tcclDeviceAllReduce(&arg);
        // char *to_be_write = cross_in + s_off;
        // tccl_move_to_list((void *)to_be_write, comm->devList + DEV_NUM, rank_num, total_offset + s_off, card_cpy_cnt
        // * typelen, stream);

        char *send = cross_in + s_off;
        flag = ++comm->sync.a2acnt;
        SDAACHECK(tcclStreamWriteValue64(stream, cntflag, flag, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        for (int ci = 1; ci < card_num; ci++) {
            int peer = (card_id + ci) % card_num;
            char *next_recv = comm->cardMap[peer].rankMap[rank_id].inBuff + off + s_off;
            SDAACHECK(
                tcclIpcMemcpyPeertoPeerAsync(next_recv, send, card_cpy_cnt * typelen, SDAA_IPC_P2P_RO_OPEN, stream));
            SDAACHECK(tcclIpcMemcpyPeertoPeerAsync(&comm->sync.a2alist[peer][card_id * DEV_NUM_MAX + rank_id], cntflag,
                                                   sizeof(uint64_t), SDAA_IPC_P2P_RO_CLOSE, stream));
        }
        for (int ci = 1; ci < card_num; ci++) {
            int peer = (card_id + ci) % card_num;
            const size_t peer_cpy_cnt = peer_cpy_cnts[peer];
            if (peer_cpy_cnt == 0) continue;
            SDAACHECK(tcclStreamWaitValue64(stream, &comm->sync.a2alist[card_id][peer * DEV_NUM_MAX + rank_id], flag,
                                            SDAA_STREAM_WAIT_VALUE_GEQ));
            // char* to_be_write = cross_in + peer * sizeslice;
            // tccl_move_to_list((void *)to_be_write, comm->devList + DEV_NUM, rank_num, total_offset + peer *
            // sizeslice, peer_cpy_cnt * typelen, stream);
        }
        char *to_be_write = cross_in;
        tccl_move_to_list((void *)to_be_write, comm->devList + DEV_NUM, rank_num, total_offset, cpy_cnt * typelen,
                          stream);

        comm->step_no++;
    }
    tcclDevSyncCard(comm, stream);
    // reset split flag
    if (ifSplit && rank_id == 0) {
        SDAACHECK(tcclStreamWriteValue64(stream, sf, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT));
    }
    return tcclSuccess;
}

/**
 * allReduceTwoNode
 * - optimize for two node allreduce
 */
template<size_t LDM_SIZE>
tcclResult_t allReduceTwoNode(tcclComm_t comm, struct collArgs *args) {
    // node info
    struct tcclNode *node = comm->node;
    // node_rank
    int rank_id = node->rank_id;
    // total rank in this node
    int rank_num = node->rank_num;
    // node id
    int card_id = comm->allRing->phyCardRank;
    // total node num
    int card_num = comm->allRing->allCardNum;

    sdaaStream_t stream = args->stream;
    // world_rank
    int rank = comm->rank;
    // total rank in world -> world_size
    int nranks = comm->nranks;
    // next world_rank
    int next_rank = comm->allRing->phyNextRank;
    // pre world_rank
    int pre_rank = comm->allRing->phyPrevRank;

    size_t total_count = args->count;
    // - data_size = count * tcclsizeof_datatype(data_type)
    size_t data_size = args->data_size;
    // type of date need to trans
    tcclDataType_t datatype = args->datatype;
    // reduce opt
    tcclRedOp_t op = args->op;

    size_t typelen = tcclsizeof_datatype(datatype);

    size_t step_len;
    tcclNicStepLen(&step_len, data_size, nranks, rank_num);

    size_t step_count = step_len / typelen * card_num;
    /* init flag*/
    uint64_t sc = comm->sync.splitCnt++;
    uint64_t *sf = &comm->sync.splitFlag[sc % SPLIT_FLAG_MAX];
    sc = comm->sync.splitCnt++;
    uint64_t *sf1 = &comm->sync.splitFlag[sc % SPLIT_FLAG_MAX];
    sc = comm->sync.splitCnt++;
    uint64_t *sf2 = &comm->sync.splitFlag[sc % SPLIT_FLAG_MAX];
    if (0 == rank_id) {
        // init
        // SDAACHECK(tcclStreamWriteValue64(stream, sf, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        // SDAACHECK(tcclStreamWriteValue64(stream, sf1, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        // SDAACHECK(tcclStreamWriteValue64(stream, sf2, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        sdaaStreamBatchMemOpParams batch_memops_params[3];
        batch_memops_params[0].type = SDAA_STREAM_MEM_OP_SET_VALUE64;
        batch_memops_params[0].addr = sf;
        batch_memops_params[0].value = 0;
        batch_memops_params[0].flags = SDAA_STREAM_WRITE_VALUE_DEFAULT;

        batch_memops_params[1].type = SDAA_STREAM_MEM_OP_SET_VALUE64;
        batch_memops_params[1].addr = sf1;
        batch_memops_params[1].value = 0;
        batch_memops_params[1].flags = SDAA_STREAM_WRITE_VALUE_DEFAULT;

        batch_memops_params[2].type = SDAA_STREAM_MEM_OP_SET_VALUE64;
        batch_memops_params[2].addr = sf2;
        batch_memops_params[2].value = 0;
        batch_memops_params[2].flags = SDAA_STREAM_WRITE_VALUE_DEFAULT;
        sdaaStreamBatchMemOp(stream, 3, batch_memops_params, 0);
    }
    /* end init flag */
    uint64_t tmpAddr = (uint64_t)comm->nic_info[rank].inBuff;
    SDAACHECK(tcclStreamWriteValue64(stream, comm->tmp, tmpAddr, SDAA_STREAM_WRITE_VALUE_DEFAULT));

    int step_no = 0;
    for (size_t cnt = rank_id * step_count; cnt < total_count; cnt += (rank_num * step_count)) {
        size_t total_offset = cnt * typelen;
        size_t offset = (((step_no++) & 1) == 0) ? 0 : (MAX_SIZE >> 1);

        char *send = comm->nic_info[rank].inBuff + offset;
        char *recv = comm->nic_info[rank].outBuff + offset;

        size_t cntslice = step_count / card_num;
        size_t sizeslice = cntslice * typelen;

        int rslice = (card_id + card_num - 1) % card_num;
        int sslice = (card_id + card_num) % card_num;

        size_t s_off = sslice * sizeslice;
        size_t r_off = rslice * sizeslice;

        size_t cpy_cnt = step_count;
        if (cpy_cnt + cnt > total_count) cpy_cnt = total_count - cnt;

        // > 1 rank per node
        if (comm->dev_num != 1) {
            args->count = cpy_cnt;
            args->data_size = cpy_cnt * typelen;
            args->s_offset = total_offset;
            args->r_offset = offset;
            args->dev_num = 1;
            args->recvList = comm->tmp;

            SDAACHECK(tcclStreamWaitValue64(stream, sf, rank_id, SDAA_STREAM_WAIT_VALUE_EQ));
            tcclDeviceAllReduce<LDM_SIZE>(args);
            SDAACHECK(tcclStreamWriteValue64(stream, sf, (rank_id + 1) % rank_num, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        } else {
            // only 2 card and 1 rank for each
            tccl_move_to_cross(args->sendusr + total_offset, send, cpy_cnt * typelen, stream);
        }

        // scatter-reduce
        {
            // send info
            SDAACHECK(tcclStreamWaitValue64(stream, sf1, rank_id, SDAA_STREAM_WAIT_VALUE_EQ));
            if (comm->recv_enable) {
                tcclRankRecvPost(offset + r_off, SEND_BUFF, sizeslice, pre_rank, comm, stream);
            } else {
                tcclRankSendPost(offset + s_off, SEND_BUFF, sizeslice, pre_rank, comm, stream);
            }
            SDAACHECK(tcclStreamWriteValue64(stream, sf1, (rank_id + 1) % rank_num, SDAA_STREAM_WRITE_VALUE_DEFAULT));

            // reduce
            tcclDeviceCalculate<LDM_SIZE>(send + r_off, recv + r_off, recv + r_off, cntslice, sizeslice, op, datatype, stream);
        }

        // allgather
        {
            rslice = card_id;
            sslice = (card_id + 1) % card_num;

            s_off = sslice * sizeslice;
            r_off = rslice * sizeslice;

            SDAACHECK(tcclStreamWaitValue64(stream, sf2, rank_id, SDAA_STREAM_WAIT_VALUE_EQ));
            if (comm->recv_enable) {
                tcclRankRecvPost(offset + r_off, RECV_BUFF, sizeslice, pre_rank, comm, stream);
            } else {
                tcclRankSendPost(offset + s_off, RECV_BUFF, sizeslice, pre_rank, comm, stream);
            }
            SDAACHECK(tcclStreamWriteValue64(stream, sf2, (rank_id + 1) % rank_num, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        }

        tccl_move_to_list(recv, comm->devList + DEV_NUM, rank_num, total_offset, cpy_cnt * typelen, stream);
    }
    tcclDevSyncCard(comm, stream);

    // reset split flag
    if (rank_id == 0) {
        sdaaStreamBatchMemOpParams batch_memops_params[3];
        batch_memops_params[0].type = SDAA_STREAM_MEM_OP_SET_VALUE64;
        batch_memops_params[0].addr = sf;
        batch_memops_params[0].value = 0;
        batch_memops_params[0].flags = SDAA_STREAM_WRITE_VALUE_DEFAULT;

        batch_memops_params[1].type = SDAA_STREAM_MEM_OP_SET_VALUE64;
        batch_memops_params[1].addr = sf1;
        batch_memops_params[1].value = 0;
        batch_memops_params[1].flags = SDAA_STREAM_WRITE_VALUE_DEFAULT;

        batch_memops_params[2].type = SDAA_STREAM_MEM_OP_SET_VALUE64;
        batch_memops_params[2].addr = sf2;
        batch_memops_params[2].value = 0;
        batch_memops_params[2].flags = SDAA_STREAM_WRITE_VALUE_DEFAULT;
        sdaaStreamBatchMemOp(stream, 3, batch_memops_params, 0);
        // SDAACHECK(tcclStreamWriteValue64(stream, sf, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT));

        // SDAACHECK(tcclStreamWriteValue64(stream, sf1, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        // SDAACHECK(tcclStreamWriteValue64(stream, sf2, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT));
    }
    return tcclSuccess;
}

tcclResult_t tcclPublicWork(tcclComm_t comm, struct collArgs *args) {
    struct tcclNode *node = comm->node;
    int rank = comm->rank;
    int rank_id = node->rank_id;
    int rank_num = node->rank_num; /* 第一个快速版本，默认各卡rank_num相同 */
    int node_num = node->node_num;
    sdaaStream_t stream = args->stream;

    uint64_t sc = comm->sync.splitCnt;
    comm->sync.splitCnt++;
    uint64_t *sf = &comm->sync.splitFlag[sc % DEV_NUM_MAX];

    if (node->card_num > 1) tcclDevSyncToPrevCard(comm, stream);

    if (args->coll == tcclCollAllReduce) {
        size_t total_count = args->count;
        size_t data_size = args->data_size;
        tcclDataType_t datatype = args->datatype;
        tcclRedOp_t op = args->op;
        int typelen = tcclsizeof_datatype(datatype);
        int card_id = node->card_id;
        int card_num = node->card_num;
        int next_card = (card_id + 1) % card_num;
        // all2all version for one node 4 card
        if (comm->isOneNode && node->card_num == 4 && rank_num == DEV_NUM) {
            return allReduceOnenodeA2A(comm, args);
        }
        if (node->sharpEnable == false || typelen == 1) {
            // optimize for 2 cards allreduce
            // only adapt float type
            if (card_num == 2 && comm->isOneNode){
                if (args->datatype == tcclFloat && data_size > FP32_DISPATCH_SIZE) return allReduceTwoNode<FLOAT_LDM_SIZE>(comm, args);
                if (args->datatype == tcclBF16 && data_size >= BF16_DISPATCH_SIZE)
                    return allReduceTwoNode<HLAF_LDM_SIZE>(comm, args);
                if (args->datatype == tcclHalf && data_size >= FP16_DISPATCH_SIZE)
                    return allReduceTwoNode<HLAF_LDM_SIZE>(comm, args);
            }
            return tcclNicPublicWork(comm, args);
        }
        int nranks = card_num * rank_num;
        size_t step_len = tcclGetStepLen(data_size, nranks);
        size_t step_count = step_len / typelen * card_num;

        // check if split needed
        bool ifSplit = true;
        if (rank_num * step_count >= total_count) {
            ifSplit = false;
        }

        // reset split flag
        if (ifSplit && rank_id == 0) {
            SDAACHECK(tcclStreamWriteValue64(stream, sf, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        }
        uint64_t tmpAddr;
        if (card_num == 1) {
            tmpAddr = (uint64_t)comm->cardMap[comm->node->card_id].rankMap[rank_id].outBuff;
        } else {
            tmpAddr = (uint64_t)comm->cardMap[comm->node->card_id].rankMap[rank_id].inBuff;
        }
        SDAACHECK(tcclStreamWriteValue64(stream, comm->tmp, tmpAddr, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        for (size_t cnt = rank_id * step_count; cnt < total_count; cnt += (rank_num * step_count)) {
            size_t total_offset = cnt * typelen;
            size_t offset = (comm->step_no++ % 2 == 0) ? 0 : (MAX_SIZE / 2);

            char *next_recv = comm->cardMap[next_card].rankMap[rank_id].outBuff + offset;
            char *send = comm->cardMap[card_id].rankMap[rank_id].inBuff + offset;
            char *recv = comm->cardMap[card_id].rankMap[rank_id].outBuff + offset;

            size_t cntslice = step_count / card_num;
            size_t sizeslice = cntslice * typelen;

            int rslice = (card_id + card_num - 1) % card_num;
            int sslice = (card_id + card_num) % card_num;

            size_t s_off = sslice * sizeslice;
            size_t r_off = rslice * sizeslice;

            size_t cpy_cnt = step_count;
            if (cpy_cnt + cnt > total_count) cpy_cnt = total_count - cnt;

            if (comm->dev_num != 1) {
                args->count = cpy_cnt;
                args->data_size = cpy_cnt * typelen;
                args->s_offset = total_offset;
                args->r_offset = offset;
                args->dev_num = 1;
                args->recvList = comm->tmp;

                // split operations for each device in one card
                if (ifSplit) SDAACHECK(tcclStreamWaitValue64(stream, sf, rank_id, SDAA_STREAM_WAIT_VALUE_EQ));
                tcclDeviceAllReduce(args);
                if (ifSplit)
                    SDAACHECK(
                        tcclStreamWriteValue64(stream, sf, (rank_id + 1) % rank_num, SDAA_STREAM_WRITE_VALUE_DEFAULT));
            } else {
                if (card_num == 1) {
                    tccl_move_to_cross(args->sendusr + total_offset, recv, cpy_cnt * typelen, stream);
                } else {
                    tccl_move_to_cross(args->sendusr + total_offset, send, cpy_cnt * typelen, stream);
                }
            }
            if (card_num > 1) {
                SDAACHECK(tcclIpcMemcpyPeertoPeerAsync(next_recv + s_off, send + s_off, sizeslice, SDAA_IPC_P2P_RO_OPEN,
                                                       stream));
                tcclDevSyncToNextCard(comm, stream);

                tcclDeviceCalculate(send + r_off, recv + r_off, recv + r_off, cntslice, sizeslice, op, datatype,
                                    stream);
            }
            for (int i = 1; i < card_num - 1; i++) {
                rslice = (card_id - i + card_num - 1) % card_num;
                sslice = (card_id - i + card_num) % card_num;
                s_off = sslice * sizeslice;
                r_off = rslice * sizeslice;

                SDAACHECK(tcclIpcMemcpyPeertoPeerAsync(next_recv + s_off, recv + s_off, sizeslice, SDAA_IPC_P2P_RO_OPEN,
                                                       stream));
                tcclDevSyncToNextCard(comm, stream);

                tcclDeviceCalculate(send + r_off, recv + r_off, recv + r_off, cntslice, sizeslice, op, datatype,
                                    stream);
            }

            if (node_num > 1) {
                struct tcclSharpArgs *data = (struct tcclSharpArgs *)malloc(sizeof(struct tcclSharpArgs));
                if (data == NULL) return tcclSystemError;
                data->comm = comm;
                data->offset = offset + r_off;
                data->count = cntslice;
                data->datatype = datatype;
                data->op = op;

                SDAACHECK(tcclLaunchHostFunc(stream, tcclAllreduceInterFunc, data));
            }

            for (int i = 0; i < card_num - 1; i++) {
                rslice = (card_id - i + card_num) % card_num;
                sslice = (card_id - i + card_num + 1) % card_num;
                s_off = sslice * sizeslice;
                r_off = rslice * sizeslice;

                SDAACHECK(tcclIpcMemcpyPeertoPeerAsync(next_recv + s_off, recv + s_off, sizeslice, SDAA_IPC_P2P_RO_OPEN,
                                                       stream));
                tcclDevSyncToNextCard(comm, stream);
            }

            tccl_move_to_list(recv, comm->devList + DEV_NUM, rank_num, total_offset, cpy_cnt * typelen, stream);
        }

        tcclDevSyncCard(comm, stream);
        // reset split flag
        if (ifSplit && rank_id == 0) {
            SDAACHECK(tcclStreamWriteValue64(stream, sf, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        }
    } else if (args->coll == tcclCollBroadcast) {
        int nranks   = comm->nranks;
        int rootRank = args->bcastroot;
        int nextRank = comm->allRing->phyNextRank;
        int prevRank = comm->allRing->phyPrevRank;

        int myCard   = comm->allRing->phyCardRank;
        int nextCard = comm->nic_info[comm->nic_info[rank].nextRank].phyCardRank;
        int rootCard = comm->nic_info[rootRank].phyCardRank;
        int cardNum  = comm->allRing->allCardNum;

        size_t dataSize  = args->data_size;
        size_t chunkSize = tcclGetBroadcastChunkSize(dataSize, rank_num);

        /* check if reuse cross buffer */
        if (dataSize > rank_num * MAX_SIZE) {
            comm->reuseCross = true;
            chunkSize        = 4 * 1024 * 1024;
        } else {
            comm->reuseCross = false;
        }

        int nChunks          = MAX_SIZE / chunkSize;
        size_t realChunkSize = chunkSize;
        int stepNo           = 0;

        for (size_t off = rank_id * chunkSize; off < dataSize; off += chunkSize * rank_num) {
            if (off + chunkSize > dataSize) realChunkSize = dataSize - off;
            int crossNo  = stepNo++ % nChunks;
            int crossOff = crossNo * chunkSize;
            char *send   = comm->nic_info[rank].inBuff + crossOff;
            char *recv   = comm->nic_info[rank].outBuff + crossOff;

            comm->sync.peerBcastCnt++;

            if (myCard == rootCard) {
                // send
                tccl_move_to_cross((void *)args->sendList, (void *)send, realChunkSize, stream, false, true, off);
                if (cardNum != 1) tcclBcastWaitAckFlag(crossNo, stepNo, nChunks, comm, stream);
                if (cardNum != 1) tcclRankSendPostNoWait(crossOff, SEND_BUFF, realChunkSize, nextRank, comm, stream);
                tccl_move_to_list(send, comm->devList + DEV_NUM, rank_num, off, realChunkSize, stream, false);
            } else if (nextCard == rootCard) {
                // recvCopy
                tcclRankWaitFlag(0, ++comm->sync.nextcnt, comm, stream);
                tccl_move_to_list(recv, comm->devList + DEV_NUM, rank_num, off, realChunkSize, stream, false);
                tcclBcastSendAckFlag(crossNo, comm->sync.peerBcastCnt, comm, stream);
            } else {
                // recvSendCopy
                tcclRankWaitFlag(0, ++comm->sync.nextcnt, comm, stream);
                comm->sync.nextcnt--;
                tcclBcastWaitAckFlag(crossNo, stepNo, nChunks, comm, stream);
                tcclRankSendPostNoWait(crossOff, RECV_BUFF, realChunkSize, nextRank, comm, stream);
                tccl_move_to_list(recv, comm->devList + DEV_NUM, rank_num, off, realChunkSize, stream, false);
                tcclBcastSendAckFlag(crossNo, comm->sync.peerBcastCnt, comm, stream);
            }
        }
        comm->sync.myBcastCnt = comm->sync.peerBcastCnt;

        tcclDevSyncCard(comm, stream);
    } else if (args->coll == tcclCollAllGather) {
        PRINTF("nic public allgather\n");
        int rank = comm->rank;
        const size_t data_size = args->data_size;

        char *cross_out = comm->nic_info[rank].outBuff;  // for send data to next card
        char *sendusr = args->sendusr;

        int card_id = comm->allRing->phyCardRank;
        int card_num = comm->allRing->allCardNum;
        const int next_peer_rank = comm->allRing->phyNextRank;
        int *phyRankList = comm->allRing->phyRankList;

        int max_size_by_cross_size = lastPowerOfTwo(MAX_SIZE / 2 / card_num);
        int suggest_size = 1024 * 1024 * 4;
        const int slice_size = suggest_size < max_size_by_cross_size ? suggest_size : max_size_by_cross_size;
        const int steps = (data_size + slice_size - 1) / slice_size;

        bool ifSplit = true;
        if (steps <= 1) { ifSplit = false; }
        if (ifSplit && rank_id == 0) { SDAACHECK(tcclStreamWriteValue64(stream, sf, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT)); }

        for (int step = 0; step < steps; step++) {
            size_t cross_base = (comm->step_no++ % 2 == 0) ? 0 : (MAX_SIZE / 2);
            const size_t slice_off = step * slice_size;
            const size_t real_size = (step + 1) * slice_size < data_size ? slice_size : data_size - (step * slice_size);

            if (ifSplit) SDAACHECK(tcclStreamWaitValue64(stream, sf, rank_id, SDAA_STREAM_WAIT_VALUE_EQ));
            tccl_move_to_cross(sendusr + slice_off, cross_out + cross_base + card_id * slice_size, real_size, stream);
            tccl_move_to_list(sendusr + slice_off, comm->devList + DEV_NUM, rank_num, slice_off + rank * data_size,
                              real_size, stream);
            if (ifSplit) SDAACHECK(tcclStreamWriteValue64(stream, sf, (rank_id + 1) % rank_num, SDAA_STREAM_WRITE_VALUE_DEFAULT));

            for (int i = 0; i < card_num - 1; i++) {
                int send_card = (card_id - i + card_num) % card_num;
                int recv_card = (card_id - i - 1 + card_num) % card_num;
                size_t s_off = send_card * slice_size + cross_base;
                size_t r_off = recv_card * slice_size + cross_base;

                tcclRankSendPost(s_off, RECV_BUFF, real_size, next_peer_rank, comm, stream);

                const size_t recvuser_off = slice_off + phyRankList[recv_card] * data_size;
                char *to_be_write = cross_out + r_off;
                tccl_move_to_list((void *)to_be_write, comm->devList + DEV_NUM, rank_num, recvuser_off, real_size,
                                  stream);
            }
        }
        tcclDevSyncCard(comm, stream);
    } else if (args->coll == tcclCollReduceScatter) {
#ifdef _SWIB_
        tcclDevSyncToLgcPrevCard(comm, stream);
#endif
        int nranks = comm->nranks;
        int myrank = comm->rank;
        int card_id = comm->allRing->phyCardRank;
        int card_num = comm->allRing->allCardNum;
        args->rank = comm->rank;
        int destCard = -1;
        size_t offset;
        const size_t data_size = args->data_size;
        int typelen = tcclsizeof_datatype(args->datatype);
        const int next_peer_rank = comm->allRing->phyNextRank;

        size_t max_size_by_cross_size = lastPowerOfTwo(MAX_SIZE / 2 / card_num);
        size_t suggest_size = 4 * 1024 * 1024;
        const size_t slice_size = suggest_size < max_size_by_cross_size ? suggest_size : max_size_by_cross_size;
        const int steps = (data_size + slice_size - 1) / slice_size;
        args->sliceSize = slice_size;

        bool ifSplit = true;
        if (steps <= 1) { ifSplit = false; }
        if (ifSplit && rank_id == 0) { SDAACHECK(tcclStreamWriteValue64(stream, sf, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT)); }

        for (int step = 0; step < steps; step++) {
            size_t cross_base = (comm->step_no++ % 2 == 0) ? 0 : (MAX_SIZE / 2);
            const size_t slice_off = step * slice_size;
            const size_t real_size = (step + 1) * slice_size < data_size ? slice_size : data_size - (step * slice_size);
            args->realSize = real_size;
            args->count = real_size / typelen;

            // move/reduce data to cross buffer
            // rank_num == 1: move sendusr data to cross buffer
            // rank_num > 1: reduce all CG's data to cross buffer
            args->sendOffsetNum = card_num;
            int firstRank = rank_id;
            args->s_offset = slice_off;
            args->r_offset = cross_base;
            args->sendnum = rank_num;
            args->recvnum = 1;
            args->recvUseCrossOut = true;
            if (ifSplit) SDAACHECK(tcclStreamWaitValue64(stream, sf, rank_id, SDAA_STREAM_WAIT_VALUE_EQ));
            tcclDeviceReduce(args);
            if (ifSplit) SDAACHECK(tcclStreamWriteValue64(stream, sf, (rank_id + 1) % rank_num, SDAA_STREAM_WRITE_VALUE_DEFAULT));
            args->recvUseCrossOut = false;
            args->sendOffsetNum = 1;

            // send
            destCard = (card_id - 1 + card_num) % card_num;
            offset = cross_base + destCard * slice_size;
            tcclRankSendPost(offset, SEND_BUFF, real_size, next_peer_rank, comm, stream);

            // recvReduceSend
            args->useCrossBuff = true;
            args->needCopy = false;
            args->sendnum = 2;
            args->recvnum = 1;
            for (int i = 2; i < card_num; i++) {
                destCard = (card_id - i + card_num) % card_num;
                offset = cross_base + destCard * slice_size;
                args->s_offset = offset;
                args->r_offset = offset;
                tcclDeviceReduce(args);
                tcclRankSendPost(offset, RECV_BUFF, real_size, next_peer_rank, comm, stream);
            }

            // recvReduceCopy
            destCard = card_id;
            offset = cross_base + destCard * slice_size;
            args->s_offset = offset;
            args->r_offset = slice_off;
            args->needCopy = true;
            tcclDeviceReduce(args);
            args->needCopy = false;
            args->useCrossBuff = false;
        }
        tcclDevSyncCard(comm, stream);
    } else if (args->coll == tcclCollReduce) {
        int nranks = comm->nranks;
        int myrank = comm->rank;
        int gCardId = comm->allRing->phyCardRank;
        int gCardNum = comm->allRing->allCardNum;
        int rootCard = comm->nic_info[args->bcastroot].phyCardRank;
        int prevCard = comm->nic_info[comm->allRing->phyPrevRank].phyCardRank;
        int firstCard = comm->nic_info[comm->nic_info[args->bcastroot].nextRank].phyCardRank;
        const size_t data_size = args->data_size;
        int typelen = tcclsizeof_datatype(args->datatype);
        const int next_peer_rank = comm->allRing->phyNextRank;
        const int prev_peer_rank = comm->allRing->phyPrevRank;

        const size_t slice_size = 4 * 1024 * 1024;
        const size_t buffer_num = MAX_SIZE / slice_size;
        const int steps = (data_size + slice_size * rank_num - 1) / (slice_size * rank_num);
        args->sliceSize = slice_size;
        int start_prev_sync = (comm->sync.prevcnt + 1) % buffer_num;

        bool ifSplit = true;
        if (steps <= 1) { ifSplit = false; }
        if (ifSplit && rank_id == 0) { SDAACHECK(tcclStreamWriteValue64(stream, sf, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT)); }

        for (int step = 0; step < steps; step++) {
            size_t cross_base = (comm->step_no % buffer_num) * slice_size;
            comm->step_no++;
            const size_t slice_off = step * (slice_size * rank_num) + slice_size * rank_id;
            if (slice_off >= data_size) continue;
            const size_t real_size = slice_off + slice_size <= data_size ? slice_size : data_size - slice_off;
            args->realSize = real_size;
            args->count = real_size / typelen;

            args->sendOffsetNum = 1;
            args->s_offset = slice_off;
            args->r_offset = cross_base;
            args->sendnum = rank_num;
            args->recvnum = 1;
            args->recvUseCrossOut = true;
            if (ifSplit) SDAACHECK(tcclStreamWaitValue64(stream, sf, rank_id, SDAA_STREAM_WAIT_VALUE_EQ));
            tcclDeviceReduce(args);
            if (ifSplit) SDAACHECK(tcclStreamWriteValue64(stream, sf, (rank_id + 1) % rank_num, SDAA_STREAM_WRITE_VALUE_DEFAULT));
            args->recvUseCrossOut = false;

            args->sendnum = 2;
            args->recvnum = 1;
            bool need_prev_sync = false;
            comm->sync.prevcnt++;
            if (step >= buffer_num && comm->sync.prevcnt % buffer_num == start_prev_sync) need_prev_sync = true;
            if (prevCard == rootCard) {
                // send
                if (need_prev_sync) tcclRankWaitFlag(1, comm->sync.prevcnt, comm, stream);
                tcclRankSendPostNoWait(cross_base, SEND_BUFF, real_size, next_peer_rank, comm, stream);
            } else if (gCardId == rootCard) {
                // recvReduceCopy
                if (need_prev_sync) tcclRankSendFlag(1, comm->sync.prevcnt, prev_peer_rank, comm, stream);
                char *flag_wait_devi = (char *)&comm->nic_info[myrank].flag_devi[0];
                SDAACHECK(
                    tcclStreamWaitValue64(stream, flag_wait_devi, ++comm->sync.nextcnt, SDAA_STREAM_WAIT_VALUE_GEQ));
                args->useCrossBuff = true;
                args->needCopy = true;
                args->s_offset = cross_base;
                args->r_offset = slice_off;
                tcclDeviceReduce(args);
                args->needCopy = false;
                args->useCrossBuff = false;
            } else {
                // recvReduceSend
                if (need_prev_sync) tcclRankSendFlag(1, comm->sync.prevcnt, prev_peer_rank, comm, stream);
                char *flag_wait_devi = (char *)&comm->nic_info[myrank].flag_devi[0];
                SDAACHECK(
                    tcclStreamWaitValue64(stream, flag_wait_devi, ++comm->sync.nextcnt, SDAA_STREAM_WAIT_VALUE_GEQ));
                comm->sync.nextcnt--;

                args->useCrossBuff = true;
                args->s_offset = cross_base;
                args->r_offset = cross_base;
                tcclDeviceReduce(args);
                if (need_prev_sync) tcclRankWaitFlag(1, comm->sync.prevcnt, comm, stream);
                tcclRankSendPostNoWait(cross_base, RECV_BUFF, real_size, next_peer_rank, comm, stream);
                args->useCrossBuff = false;
            }
        }
        tcclDevSyncCard(comm, stream);
        if (ifSplit && rank_id == 0) {
            SDAACHECK(tcclStreamWriteValue64(stream, sf, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        }
    } else if (args->coll == tcclCollAlltoall) {
        int nranks = comm->nranks;
        char *send = comm->nic_info[rank].inBuff;
        char *recv = comm->nic_info[rank].outBuff;
        size_t data_size = args->data_size;
        char *sendusr = args->sendusr;
        char *recvusr = args->recvusr;

        tccl_move_to_cross_pro(sendusr + rank*data_size, recvusr + rank*data_size, data_size, stream);

        for (int i = 1; i < nranks; i++) {
            int slice_size = MAX_SIZE;
            int peer = (rank + i) % nranks;
            int recv_step = (rank - i + nranks) % nranks;
            int total_off = peer * data_size;

            for (int off = 0; off < data_size; off += slice_size) {
                if (off + slice_size > data_size) slice_size = data_size - off;

                tcclSendRecvNotify(comm, recv_step, stream);
                tccl_move_to_cross_pro(sendusr + total_off + off, send, slice_size, stream);
                tcclSendRecvWait(comm, peer, stream);

                tcclRankSendPostNoWait(0, SEND_BUFF, slice_size, peer, comm, stream, false);

                tcclSendRecvNotify(comm, peer, stream);
                tcclSendRecvWait(comm, recv_step, stream);
                tccl_move_to_cross_pro(recv, recvusr + (recv_step * data_size) + off, slice_size, stream);

                tcclDevSyncCard(comm, stream);
                tcclRankSendFlag(3, ++comm->sync.all2allcnt, comm->allRing->phyNextRank, comm, stream);
                tcclRankWaitFlag(3, comm->sync.all2allcnt, comm, stream);
            }
        }
    } else {
    }
    return tcclSuccess;
}

tcclResult_t tcclNicStepLen(size_t *slice, int data_size, int nranks, int rank_num) {
    int maxSlice = MAX_SIZE / 2 / nranks * rank_num;
    maxSlice = maxSlice / ALIGNED_BYTE * ALIGNED_BYTE;
    if (maxSlice >= 8 * 1024 * 1024) {
        maxSlice = 8 * 1024 * 1024;
    }

    int dataSizePerRank = data_size / nranks;
    if (dataSizePerRank >= maxSlice) {
        *slice = maxSlice;
        return tcclSuccess;
    }

    if (dataSizePerRank <= 128 * 1024) {
        *slice = 128 * 1024;
    } else if (dataSizePerRank <= 256 * 1024) {
        *slice = 256 * 1024;
    } else if (dataSizePerRank <= 512 * 1024) {
        *slice = 512 * 1024;
    } else if (dataSizePerRank <= 640 * 1024) {
        *slice = 640 * 1024;
    } else if (dataSizePerRank <= 768 * 1024) {
        *slice = 768 * 1024;
    } else if (dataSizePerRank <= 1024 * 1024) {
        *slice = 1024 * 1024;
    } else if (dataSizePerRank <= 1536 * 1024) {
        *slice = 1536 * 1024;
    } else if (dataSizePerRank <= 2 * 1024 * 1024) {
        *slice = 2 * 1024 * 1024;
    } else if (dataSizePerRank <= 4 * 1024 * 1024) {
        *slice = 4 * 1024 * 1024;
    } else if (dataSizePerRank <= 6 * 1024 * 1024) {
        *slice = 6 * 1024 * 1024;
    } else if (dataSizePerRank <= maxSlice) {
        *slice = maxSlice;
    }

    return tcclSuccess;
}

tcclResult_t tcclNicPublicWork(tcclComm_t comm, struct collArgs *args) {
    struct tcclNode *node = comm->node;
    int rank_id = node->rank_id;
    int rank_num = node->rank_num; /* 第一个快速版本，默认各卡rank_num相同 */
    int card_id = comm->allRing->phyCardRank;
    int card_num = comm->allRing->allCardNum;
    sdaaStream_t stream = args->stream;
    int rank = comm->rank;
    int nranks = comm->nranks;
    int next_rank = comm->allRing->phyNextRank;

#ifdef _SWIB_
    // Prev ring sync to prevent data from being overwritten.
    tcclDevSyncToPrevCard(comm, stream);
#endif

    if (args->coll == tcclCollAllReduce) {
        PRINTF("tcclNicPublicWork\n");

#ifdef _SWIB_
        // 6B use double ring.
        if (node->rank_id == 1 || node->rank_id == 3) {
            card_id = comm->allRing->allCardNum - 1 - comm->allRing->phyCardRank;
            card_num = comm->allRing->allCardNum;
            next_rank = comm->allRing->phyPrevRank;

            // Do a next ring sync for double ring.
            tcclRankSendFlag(2, ++comm->sync.nextcnt, comm->allRing->phyNextRank, comm, stream);
            tcclRankWaitFlag(2, comm->sync.nextcnt, comm, stream);
        }
#endif

        size_t total_count = args->count;
        size_t data_size = args->data_size;
        tcclDataType_t datatype = args->datatype;
        tcclRedOp_t op = args->op;
        int typelen = tcclsizeof_datatype(datatype);

        size_t step_len;
        tcclNicStepLen(&step_len, data_size, nranks, rank_num);

        if (rank == 0) PRINTF("step_len:%f\n", (float)step_len / 1024 / 1024);
        size_t step_count = step_len / typelen * card_num;

#ifndef _SWIB_
        bool ifSplit = true;
        if (rank_num * step_count >= total_count) {
            ifSplit = false;
        }
#else
        bool ifSplit = false;
#endif
        uint64_t sc = comm->sync.splitCnt;
        comm->sync.splitCnt++;
        uint64_t *sf = &comm->sync.splitFlag[sc % DEV_NUM_MAX];

        // reset split flag
        if (ifSplit && rank_id == 0) {
            SDAACHECK(tcclStreamWriteValue64(stream, sf, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        }

        uint64_t tmpAddr = (uint64_t)comm->nic_info[rank].inBuff;
        SDAACHECK(tcclStreamWriteValue64(stream, comm->tmp, tmpAddr, SDAA_STREAM_WRITE_VALUE_DEFAULT));

        int step_no = 0;
        for (size_t cnt = rank_id * step_count; cnt < total_count; cnt += (rank_num * step_count)) {
            size_t total_offset = cnt * typelen;
            size_t offset = (step_no++ % 2 == 0) ? 0 : (MAX_SIZE / 2);

            char *send = comm->nic_info[rank].inBuff + offset;
            char *recv = comm->nic_info[rank].outBuff + offset;

            size_t cntslice = step_count / card_num;
            size_t sizeslice = cntslice * typelen;

            int rslice = (card_id + card_num - 1) % card_num;
            int sslice = (card_id + card_num) % card_num;

            size_t s_off = sslice * sizeslice;
            size_t r_off = rslice * sizeslice;

            size_t cpy_cnt = step_count;
            if (cpy_cnt + cnt > total_count) cpy_cnt = total_count - cnt;

            if (comm->dev_num != 1) {
                args->count = cpy_cnt;
                args->data_size = cpy_cnt * typelen;
                args->s_offset = total_offset;
                args->r_offset = offset;
                args->dev_num = 1;
                args->recvList = comm->tmp;

                // split operations for each device in one card
                if (ifSplit) SDAACHECK(tcclStreamWaitValue64(stream, sf, rank_id, SDAA_STREAM_WAIT_VALUE_EQ));
                tcclDeviceAllReduce(args);
                if (ifSplit)
                    SDAACHECK(
                        tcclStreamWriteValue64(stream, sf, (rank_id + 1) % rank_num, SDAA_STREAM_WRITE_VALUE_DEFAULT));
            } else {
                if (card_num == 1) {
                    tccl_move_to_cross(args->sendusr + total_offset, recv, cpy_cnt * typelen, stream);
                } else {
                    tccl_move_to_cross(args->sendusr + total_offset, send, cpy_cnt * typelen, stream);
                }
            }

            if (card_num > 1) {
                tcclRankSendPost(offset + s_off, SEND_BUFF, sizeslice, next_rank, comm, stream);
                tcclDeviceCalculate(send + r_off, recv + r_off, recv + r_off, cntslice, sizeslice, op, datatype,
                                    stream);
            }

            for (int i = 1; i < card_num - 1; i++) {
                rslice = (card_id - i + card_num - 1) % card_num;
                sslice = (card_id - i + card_num) % card_num;

                s_off = sslice * sizeslice;
                r_off = rslice * sizeslice;

                tcclRankSendPost(offset + s_off, RECV_BUFF, sizeslice, next_rank, comm, stream);
                tcclDeviceCalculate(send + r_off, recv + r_off, recv + r_off, cntslice, sizeslice, op, datatype,
                                    stream);
            }

            for (int i = 0; i < card_num - 1; i++) {
                rslice = (card_id - i + card_num) % card_num;
                sslice = (card_id - i + card_num + 1) % card_num;

                s_off = sslice * sizeslice;
                r_off = rslice * sizeslice;

                tcclRankSendPost(offset + s_off, RECV_BUFF, sizeslice, next_rank, comm, stream);
            }
            tccl_move_to_list(recv, comm->devList + DEV_NUM, rank_num, total_offset, cpy_cnt * typelen, stream);
        }
        tcclDevSyncCard(comm, stream);
        // reset split flag
        if (ifSplit && rank_id == 0) {
            SDAACHECK(tcclStreamWriteValue64(stream, sf, 0, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        }
    } else {
    }

    return tcclSuccess;
}

tcclResult_t tcclPublicInit(tcclComm_t comm) {
    struct tcclNode *node = comm->node;
    int card_num = node->card_num;
    int rank_num = node->rank_num;
    int card_id = node->card_id;
    int rank_id = node->rank_id;

    TCCLCHECK(tcclCalloc(&comm->cardMap, card_num));

    char name[TCCL_SHM_NAME_LEN];
    snprintf(name, sizeof(name), "Map-%d", node->node_id);
    struct tcclCardAddrMap *map = (struct tcclCardAddrMap *)tcclShmAlloc(name, comm->port, card_num * sizeof(struct tcclCardAddrMap));
    if (map == NULL) {
        return tcclSystemError;
    }

    int devid = comm->devid;
    map[card_id].rankMap[rank_id].root_devid = devid;
    map[card_id].rankMap[rank_id].root_rank = comm->rank;
    map[card_id].rankMap[rank_id].inBuff = comm->addr.inBuff;
    map[card_id].rankMap[rank_id].outBuff = comm->addr.outBuff;
    map[card_id].rankMap[rank_id].inHandle = comm->addr.inHandle;
    map[card_id].rankMap[rank_id].outHandle = comm->addr.outHandle;

    if (rank_id == 0) {
        SDAACHECK(sdaaMallocCross((void **)&map[card_id].smallBuff, SMALL_BUFF_SIZE));
        SDAACHECK(sdaaIpcGetMemHandle(&map[card_id].smallHandle, map[card_id].smallBuff));
        map[card_id].pid = comm->pid;
    }

    tcclBarrierNode(comm);
    memcpy(comm->cardMap, map, card_num * sizeof(struct tcclCardAddrMap));

    for (int i = 0; i < card_num; i++) {
        if (i == card_id && rank_id == 0) continue;
        if (comm->pid != comm->cardMap[i].pid) {
            SDAACHECK(sdaaIpcOpenMemHandle((void **)&comm->cardMap[i].smallBuff, comm->cardMap[i].smallHandle, 0));
        }
    }

    SDAACHECK(sdaaMallocCross((void **)&comm->tmp, sizeof(void *)));
    tcclBarrierNode(comm);

    if (node->sharpEnable == true) {
        tcclGetSharpAddr(comm);
    }

    tcclShmFree(map, card_num * sizeof(struct tcclCardAddrMap));
    tcclBarrierNode(comm);
    return tcclSuccess;
}

tcclResult_t tcclCrossBuffInit(tcclComm_t comm) {
    char *inBuff, *outBuff;
    char *inBuff_host, *outBuff_host;
    tcclReuseMemAllocMap(&inBuff, &inBuff_host, &outBuff, &outBuff_host, comm->devid);

    comm->addr.inBuff = inBuff;
    comm->addr.inBuff_host = inBuff_host;
    comm->addr.outBuff = outBuff;
    comm->addr.outBuff_host = outBuff_host;

    SDAACHECK(sdaaIpcGetMemHandle(&comm->addr.inHandle, inBuff));
    SDAACHECK(sdaaIpcGetMemHandle(&comm->addr.outHandle, outBuff));
#ifdef _SWIB_
    char *inPcie, *outPcie;
    SDAACHECK(sdaaCrossMemToBusMem((void *)inBuff, (uint64_t *)&inPcie));
    SDAACHECK(sdaaCrossMemToBusMem((void *)outBuff, (uint64_t *)&outPcie));
    comm->addr.inPcie = inPcie;
    comm->addr.outPcie = outPcie;
#endif

    return tcclSuccess;
}

tcclResult_t tcclPublicFini(tcclComm_t comm) {
    // TODO
    tcclReuseMemFree(comm->devid);

    for (int i = 0; i < comm->node->card_num; i++) {
        if (i == comm->node->card_id && comm->node->rank_id == 0) continue;
        if (comm->pid != comm->cardMap[i].pid) {
            SDAACHECK(sdaaIpcCloseMemHandle(comm->cardMap[i].smallBuff));
        }
    }

    if (comm->node->rank_id == 0) {
        SDAACHECK(sdaaFree(comm->cardMap[comm->node->card_id].smallBuff));
    }
    SDAACHECK(sdaaFree(comm->tmp));
    return tcclSuccess;
}
