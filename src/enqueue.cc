#include <unistd.h>
#include <sys/prctl.h>
#include "collectives.h"
#include "enqueue.h"
#include "shm.h"
#include "proc.h"
#include "perf.h"
#include "debug.h"

template <typename T> static inline bool isPowerOfTwo(T val) {
    // do SmallSizeAllreduce when there is one card on SWIB
#ifdef _SWIB_
    return (val == 1);
#endif

    return (val & (val - 1)) == 0;
}
// only for 8 card situation
inline tcclResult_t tcclSmallSizeAllreduceCard_8C(tcclComm_t comm, struct collArgs *args, GraphManager *graph_manager) {
    if (TCCL_UNLIKELY(graph_manager == nullptr)) {
        TCCL_ERROR_LOG("Graph manager Should Not be Null");
        return tcclInternalError;
    }
    sdaaStream_t stream = args->stream;
    int card_id = comm->node->card_id;
    int card_num = comm->node->card_num;
    int data_size = args->data_size;
    size_t off = (comm->graph_step_no % 2) * (SMALL_BUFF_SIZE / 2);
    char *send = comm->cardMap[card_id].smallBuff + off;
    int step = 0;
    char *sendList[MAX_CARD_NUM];
    // 1. the first 2 iter, just trans
    int stride;
    for (stride = 1; stride < card_num / 2; stride *= 2) {
        const int stride_width = stride * 2;
        const int peer_id = (card_id + stride) % stride_width + card_id / stride_width * stride_width;
        int offset = card_id / stride * stride * data_size;

        char *peer_recv = comm->cardMap[peer_id].smallBuff + off;

        tcclGraphP2pNode(send + offset, peer_recv + offset, stride * data_size, SDAA_IPC_P2P_RO_OPEN);

        uint64_t flag = ++comm->sync.peercnt;
        int flag_id = flag % DEV_NUM;
        void *cntflag = &comm->sync.cntflag[step];
        tcclGraphP2pNode(cntflag, &comm->sync.peerlist[peer_id][flag_id], sizeof(uint64_t), SDAA_IPC_P2P_RO_CLOSE);
        tcclGraphMemOpsNode(SDAA_STREAM_MEM_OP_WAIT_VALUE64, &comm->sync.peerlist[card_id][flag_id], flag, SDAA_STREAM_WAIT_VALUE_GEQ);

        step++;
    }
    int offset = card_id / stride * stride * data_size;

    // 2. allreduce the first 2 iter data
    struct collArgs arg = { 0 };
    for (int j = 0; j < stride; j++)
        sendList[j] = send + offset + j * data_size;
    char* recvList = send + card_id * data_size;
    arg.sendnum = stride;
    arg.recvnum = 1;
    arg.count = args->count;
    arg.data_size = data_size;
    arg.op = args->op;
    arg.datatype = args->datatype;
    arg.useCrossBuff = true;
    arg.recvUseCrossOut = true;
    arg.sendList = (void **)sendList;
    arg.recvList = (void **)(&recvList);
    arg.stream = stream;
    arg.caltype = CAL_ALL;
    tcclDeviceAllReduce(&arg, graph_manager);

    // 3. trans the computed data
    const int stride_width = stride * 2;  // 8
    const int peer_id = (card_id + stride) % stride_width + card_id / stride_width * stride_width;
    offset = card_id * data_size;
    char *peer_recv = comm->cardMap[peer_id].smallBuff + off;
    tcclGraphP2pNode(send + offset, peer_recv + offset, data_size, SDAA_IPC_P2P_RO_OPEN);
    uint64_t flag = ++comm->sync.peercnt;
    int flag_id = flag % DEV_NUM;
    void *cntflag = &comm->sync.cntflag[step];
    tcclGraphP2pNode(cntflag, &comm->sync.peerlist[peer_id][flag_id], sizeof(uint64_t), SDAA_IPC_P2P_RO_CLOSE);
    tcclGraphMemOpsNode(SDAA_STREAM_MEM_OP_WAIT_VALUE64, &comm->sync.peerlist[card_id][flag_id], flag, SDAA_STREAM_WAIT_VALUE_GEQ);
    step++;

    // 4. allreduce the last data
    sendList[0] = send + card_id * data_size;
    sendList[1] = send + peer_id * data_size;
    arg.sendnum = 2;
    arg.count = args->count;
    arg.data_size = data_size;
    arg.op = args->op;
    arg.datatype = args->datatype;
    arg.useCrossBuff = true;
    arg.recvUseCrossOut = false;
    arg.sendList = (void **)sendList;
    arg.recvList = (void **)comm->devList + DEV_NUM;
    arg.recvnum = comm->dev_num;
    arg.caltype = CAL_ALL;
    tcclDeviceAllReduce(&arg, graph_manager);

    return tcclSuccess;
}

// calc & translate for every iteration (128KB < size)
inline tcclResult_t tcclSmallSizeAllreduceCard_CT(tcclComm_t comm, struct collArgs *args, GraphManager *graph_manager) {
    if (TCCL_UNLIKELY(graph_manager == nullptr)) {
        TCCL_ERROR_LOG("Graph manager Should Not be Null");
        return tcclInternalError;
    }
    sdaaStream_t stream = args->stream;
    int card_id = comm->node->card_id;
    int card_num = comm->node->card_num;
    int data_size = args->data_size;
    size_t off = (comm->graph_step_no % 2) * (SMALL_BUFF_SIZE / 2);
    char *send = comm->cardMap[card_id].smallBuff + off;
    int step = 0;
    char *sendList[MAX_CARD_NUM];
    // 1. the first 2 iter, just trans
    int stride;
    for (stride = 1; stride < card_num; stride *= 2) {
        const int stride_width = stride * 2;
        const int peer_id = (card_id + stride) % stride_width + card_id / stride_width * stride_width;
        int offset = card_id * data_size;

        char *peer_recv = comm->cardMap[peer_id].smallBuff + off;

        tcclGraphP2pNode(send + offset, peer_recv + offset, data_size, SDAA_IPC_P2P_RO_OPEN);

        uint64_t flag = ++comm->sync.peercnt;
        int flag_id = flag % DEV_NUM;
        void *cntflag = &comm->sync.cntflag[step];
        tcclGraphP2pNode(cntflag, &comm->sync.peerlist[peer_id][flag_id], sizeof(uint64_t), SDAA_IPC_P2P_RO_CLOSE);
        tcclGraphMemOpsNode(SDAA_STREAM_MEM_OP_WAIT_VALUE64, &comm->sync.peerlist[card_id][flag_id], flag, SDAA_STREAM_WAIT_VALUE_GEQ);

        struct collArgs arg = { 0 };
        sendList[0] = send + card_id * data_size;
        sendList[1] = send + peer_id * data_size;
        char* recvList = send + card_id * data_size;
        arg.sendnum = 2;
        arg.recvnum = stride == card_num / 2 ? comm->dev_num : 1;
        arg.count = args->count;
        arg.data_size = data_size;
        arg.op = args->op;
        arg.datatype = args->datatype;
        arg.sendList = (void **)sendList;
        arg.useCrossBuff = true;
        if (stride == card_num / 2){
            arg.recvUseCrossOut = false;
            arg.recvList = comm->devList + DEV_NUM;
        }else{
            arg.recvUseCrossOut = true;
            arg.recvList = (void **)&recvList;
        }
        arg.stream = stream;
        arg.caltype = CAL_ALL;
        tcclDeviceAllReduce(&arg, graph_manager);
        step++;
    }
    return tcclSuccess;
}

inline tcclResult_t tcclSmallSizeAllreduceCard(tcclComm_t comm, struct collArgs *args, GraphManager *graph_manager) {
    if (TCCL_UNLIKELY(graph_manager == nullptr)) {
        TCCL_ERROR_LOG("Graph manager Should Not be Null");
        return tcclInternalError;
    }
    sdaaStream_t stream = args->stream;
    int card_id = comm->node->card_id;
    int card_num = comm->node->card_num;
    int data_size = args->data_size;
    size_t off = (comm->graph_step_no % 2) * (SMALL_BUFF_SIZE / 2);
    char *send = comm->cardMap[card_id].smallBuff + off;
    int step = 0;
    for (int stride = 1; stride < card_num; stride *= 2) {
        const int stride_width = stride * 2;
        const int peer_id = (card_id + stride) % stride_width + card_id / stride_width * stride_width;
        int offset = card_id / stride * stride * data_size;

        char *peer_recv = comm->cardMap[peer_id].smallBuff + off;

        tcclGraphP2pNode(send + offset, peer_recv + offset, stride * data_size, SDAA_IPC_P2P_RO_OPEN);

        uint64_t flag = ++comm->sync.peercnt;
        int flag_id = flag % DEV_NUM;
        void *cntflag = &comm->sync.cntflag[step];
        tcclGraphP2pNode(cntflag, &comm->sync.peerlist[peer_id][flag_id], sizeof(uint64_t), SDAA_IPC_P2P_RO_CLOSE);
        tcclGraphMemOpsNode(SDAA_STREAM_MEM_OP_WAIT_VALUE64, &comm->sync.peerlist[card_id][flag_id], flag, SDAA_STREAM_WAIT_VALUE_GEQ);
        step++;
    }

    struct collArgs arg = { 0 };
    char *sendList[MAX_CARD_NUM];

    for (int j = 0; j < card_num; j++) sendList[j] = send + j * data_size;

    arg.sendnum = card_num;
    arg.recvnum = comm->dev_num;
    arg.count = args->count;
    arg.data_size = data_size;
    arg.op = args->op;
    arg.datatype = args->datatype;
    arg.sendList = (void **)sendList;
    arg.useCrossBuff = true;
    arg.recvUseCrossOut = false;
    arg.recvList = (void **)comm->devList + DEV_NUM;
    arg.stream = stream;
    arg.caltype = CAL_ALL;
    tcclDeviceAllReduce(&arg, graph_manager);
    return tcclSuccess;
}

/* halving-doubling algorithm */
inline tcclResult_t tcclSmallSizeBFMut(tcclComm_t comm, const struct collArgs *args, GraphManager *graph_manager) {
    if (TCCL_UNLIKELY(graph_manager == nullptr)) {
        TCCL_ERROR_LOG("Graph manager Should Not be Null");
        return tcclInternalError;
    }
    sdaaStream_t stream = args->stream;
    int card_id = comm->node->card_id;
    int card_num = comm->node->card_num;
    const int typelen = tcclsizeof_datatype(args->datatype);
    size_t data_size = args->data_size;
    size_t off = (comm->graph_step_no % 2) * (SMALL_BUFF_SIZE / 2);
    char *send = comm->cardMap[card_id].smallBuff + off;
    int step = 0;
    char *sendList[MAX_CARD_NUM];
    int stride;
    size_t trans_dsize = data_size / 2;
    size_t step_offset = 0;
    for (stride = 1; stride < card_num; stride *= 2) {
        const int stride_width = stride * 2;
        const int peer_id = (card_id + stride) % stride_width + card_id / stride_width * stride_width;
        const int is_right = card_id % stride_width + stride < stride_width ? 0 : 1;

        // it is a fact that card and peer have the same step_offset !!!
        int peer_step_offset = peer_id * data_size + step_offset + (is_right ? 0 : trans_dsize);
        int card_step_offset = card_id * data_size + step_offset + (is_right ? 0 : trans_dsize);

        char *peer_recv = comm->cardMap[peer_id].smallBuff + off;

        uint64_t flag = ++comm->sync.peercnt;
        int flag_id = flag % DEV_NUM;
        /* wait my peerlist flag ready to be send */
        void *cntflag = &comm->sync.cntflag[step];
        tcclGraphMemOpsNode(SDAA_STREAM_MEM_OP_WAIT_VALUE64, cntflag, flag, SDAA_STREAM_WAIT_VALUE_EQ);
        tcclGraphP2pNode(send + card_step_offset, peer_recv + card_step_offset, trans_dsize, SDAA_IPC_P2P_RO_OPEN);
        tcclGraphP2pNode(cntflag, &comm->sync.peerlist[peer_id][flag_id], sizeof(uint64_t), SDAA_IPC_P2P_RO_CLOSE);

        step_offset += is_right * trans_dsize;
        trans_dsize = trans_dsize / 2;
        step++;
    }
    for (stride /= 2; stride >= 1; stride /= 2) {
        trans_dsize = trans_dsize * 2;
        const int stride_width = stride * 2;
        const int peer_id = (card_id + stride) % stride_width + card_id / stride_width * stride_width;
        const int is_right = card_id % stride_width + stride < stride_width ? 0 : 1;

        step_offset -= is_right * trans_dsize;
        int peer_step_offset = peer_id * data_size + step_offset + (is_right ? trans_dsize : 0);
        int card_step_offset = card_id * data_size + step_offset + (is_right ? trans_dsize : 0);
        char *peer_recv = comm->cardMap[peer_id].smallBuff + off;

        uint64_t flag = ++comm->sync.peercnt;
        int flag_id = flag % DEV_NUM;
        void *cntflag = &comm->sync.cntflag[step];
        tcclGraphMemOpsNode(SDAA_STREAM_MEM_OP_WAIT_VALUE64, cntflag, flag, SDAA_STREAM_WAIT_VALUE_EQ);
        tcclGraphP2pNode(send + card_step_offset, peer_recv + peer_step_offset, trans_dsize, SDAA_IPC_P2P_RO_OPEN);

        tcclGraphP2pNode(cntflag, &comm->sync.peerlist[peer_id][flag_id], sizeof(uint64_t), SDAA_IPC_P2P_RO_CLOSE);
        step++;
    }
    return tcclSuccess;
}

inline tcclResult_t tcclSmallSizeAllreduce(struct collArgs *args, tcclComm_t comm, sdaaStream_t stream) {
    int card_num = comm->node->card_num;
    int card_id = comm->node->card_id;
    int rank_id = comm->node->rank_id;
    int dev_num = comm->dev_num;
    args->dev_num = comm->dev_num;

    void** comm_list = comm->list + (comm->graph_step_no % 2) * DEV_NUM_MAX * 2;
    comm_list[rank_id] = args->sendusr;
    comm_list[rank_id + DEV_NUM] = args->recvusr;

    sdaaStreamBatchMemOpParams batch_memops_params[3];
    args->sendList = comm->devList;
    batch_memops_params[0].type  = SDAA_STREAM_MEM_OP_SET_VALUE64;
    batch_memops_params[0].addr  = (void*)&args->sendList[rank_id];
    batch_memops_params[0].value = (uint64_t)args->sendusr;
    batch_memops_params[0].flags = SDAA_STREAM_WRITE_VALUE_DEFAULT;

    args->recvList = comm->devList + DEV_NUM;
    batch_memops_params[1].type  = SDAA_STREAM_MEM_OP_SET_VALUE64;
    batch_memops_params[1].addr  = (void*)&args->recvList[rank_id];
    batch_memops_params[1].value = (uint64_t)args->recvusr;
    batch_memops_params[1].flags = SDAA_STREAM_WRITE_VALUE_DEFAULT;

    uint64_t count = comm->sync.syncnt;
    uint64_t *syncflag = &comm->sync.syncflag[count % SYNC_NUM];
    batch_memops_params[2].type  = SDAA_STREAM_MEM_OP_SET_VALUE64;
    batch_memops_params[2].addr  = (void*)syncflag;
    batch_memops_params[2].value = 1;
    batch_memops_params[2].flags = SDAA_STREAM_WRITE_VALUE_ADD;
    sdaaStreamBatchMemOp(stream, 3, batch_memops_params, 0);
    args->useCrossBuff = false; // indicate that sendusr is not crossbuf, so use cross to store

    args->sendnum = dev_num;
    if (comm->nranks == dev_num) {
        args->recvList = comm->devList + DEV_NUM;
        args->recvUseCrossOut = false;
        args->recvnum = dev_num;
    } else {
        size_t off = (comm->graph_step_no % 2) * (SMALL_BUFF_SIZE / 2);
        char *buffer = comm->cardMap[card_id].smallBuff + off + card_id * args->data_size;
        args->recvList = (void **)&buffer;
        args->recvUseCrossOut = true;
        args->recvnum = 1;
    }


    bool useResKernel = false;
    GraphManager * graph_manager;
    if ((card_num == 8) && (args->data_size >= 32 * 1024) && (args->data_size <= 128 * 1024)){
        graph_manager = comm->gm_calc_last;
    }else if ((args->data_size > 128 * 1024) && (args->count % 4 == 0) && (card_num == 8) && \
        (args->datatype != tcclInt8) && (args->datatype != tcclUint8)){
        graph_manager = comm->gm_btmut;
        useResKernel = true;
    }else if ((args->data_size > 128 * 1024) && (card_num == 4 || card_num == 8)){
        graph_manager = comm->gm_ct;
    }else{
        graph_manager = comm->gm_normal;
    }

    tcclDevSyncRoot(comm, stream, graph_manager);
    if (comm->node->rank_id == 0) {
        /*********** prepare the reside kernel *************/
        args->flagnum = card_num == 8 ? 6 : (card_num == 4 ? 4 : (card_num == 2 ? 2 : 0));
        args->cntflag = comm->sync.cntflag;
        args->peercnt = comm->sync.peercnt;

        int rank_num = comm->node->rank_num;
        uint64_t count = comm->sync.syncnt;
        comm->sync.syncnt++;
        uint64_t *syncflag = &comm->sync.syncflag[count % SYNC_NUM];
        uint64_t *syncflag_addr = syncflag;
        uint64_t flag = count / SYNC_NUM + 1;
        if (useResKernel){
            struct resideKernelArgs rk_args;
            memset(&rk_args, 0, sizeof(struct resideKernelArgs));
            rk_args.coll_args = args;
            const int card_id = comm->node->card_id;
            rk_args.card_id = card_id;
            rk_args.card_num = comm->node->card_num;
            rk_args.my_card_flag = comm->sync.peerlist[card_id];
            const size_t off = (comm->graph_step_no % 2) * (SMALL_BUFF_SIZE / 2);
            rk_args.smallBuff = comm->cardMap[card_id].smallBuff + off;
            rk_args.recvusrs = comm->devList + DEV_NUM;

            rk_args.syncflag_addr = syncflag_addr;
            rk_args.syncflag_value = flag * rank_num;
            tcclDeviceAllReduceResideKernel(&rk_args, graph_manager);
        }else{
            tcclDeviceAllReduce(args, graph_manager);
        }

        if (comm->nranks != dev_num) {
            if ((card_num == 8) && (args->data_size >= 32 * 1024) && (args->data_size <= 128 * 1024)) {
                tcclSmallSizeAllreduceCard_8C(comm, args, graph_manager);
            } else if ((args->data_size > 128 * 1024) && (args->count % 4 == 0) && (card_num == 8) && \
                (args->datatype != tcclInt8) && (args->datatype != tcclUint8)){
                tcclSmallSizeBFMut(comm, args, graph_manager);
            } else if ((args->data_size > 128 * 1024) && (card_num == 4 || card_num == 8)) {
                tcclSmallSizeAllreduceCard_CT(comm, args, graph_manager);
            } else {
                tcclSmallSizeAllreduceCard(comm, args, graph_manager);
            }
        }
        if (useResKernel) {
            /* wait reside kenel set the flag, means reside kernel finish*/
            tcclGraphMemOpsNode(SDAA_STREAM_MEM_OP_WAIT_VALUE64, syncflag_addr, rank_num * flag, SDAA_STREAM_WAIT_VALUE_EQ);
        }else{
            /* write this flag to notify other cgs */
            tcclGraphMemOpsNode(SDAA_STREAM_MEM_OP_SET_VALUE64, syncflag, rank_num * flag, SDAA_STREAM_WRITE_VALUE_DEFAULT);
        }
        /************ launch the graph ***********/
        if (TCCL_UNLIKELY(graph_manager == nullptr)) {
            TCCL_ERROR_LOG("Graph manager Should Not be Null");
            return tcclInternalError;
        }
        if (TCCL_UNLIKELY(graph_manager->endModifyGraph() != tcclSuccess)) {
            TCCL_ERROR_LOG("all graph nodes should be modified!");
            return tcclInternalError;
        }
        tcclLaunchGraph(stream);
    }else{
        int rank_num = comm->node->rank_num;
        uint64_t count = comm->sync.syncnt;
        comm->sync.syncnt++;
        uint64_t *syncflag = &comm->sync.syncflag[count % SYNC_NUM];
        uint64_t flag = count / SYNC_NUM + 1;
        SDAACHECK(tcclStreamWaitValue64(stream, syncflag, rank_num * flag, SDAA_STREAM_WAIT_VALUE_EQ));
    }
    //tcclDevSyncOther(comm, stream, graph_manager);
    comm->graph_step_no++;
    return tcclSuccess;
}

void *tcclEnqueueCheck_func(void *arg) {
    struct tcclInfo *info = (struct tcclInfo *)arg;
    struct collArgs args = { 0 };
    void *sendusr, *recvusr;
    tcclComm_t comm = info->comm;
    sdaaStream_t stream = info->stream;
    comm->user_stream = info->stream;
    int rank, nranks;
    int dev_num = comm->dev_num;

    comm->optcnt++;

    rank = comm->rank;
    nranks = comm->nranks;

    args.rank = rank;
    args.stream = stream;
    args.datatype = info->datatype;
    args.count = info->count;
    args.op = info->op;
    args.coll = info->coll;
    args.bcastroot = info->root;
    sendusr = info->sendbuf;
    recvusr = info->recvbuf;

    size_t data_size;
    size_t count = info->count;
    int typelen = tcclsizeof_datatype(info->datatype);

    data_size = count * typelen;
    args.data_size = data_size;

    if (nranks == 1) {
        if (recvusr != sendusr) tccl_move_to_cross(sendusr, recvusr, data_size, stream);
        return NULL;
    }
    int card_id = comm->node->card_id;
    int rank_id = comm->node->rank_id;

    if (info->coll == tcclCollAllReduce) {
        args.rank_id = rank_id;
        args.sendusr = (char *)sendusr;
        args.recvusr = (char *)recvusr;
        args.nranks = nranks;

        if ((dev_num == DEV_NUM_MAX) && (typelen == 1 || typelen == 2 || typelen == 4) && (args.datatype != tcclBF16)
            && (args.op == tcclSum) && (data_size <= SMALL_SIZE) && (comm->isOneNode == true) && (isPowerOfTwo(comm->node->card_num))) {
            args.caltype = CAL_ALL;
            if (args.data_size % ALIGNED_BYTE != 0) {
                args.data_size = (args.data_size / ALIGNED_BYTE + 1) * ALIGNED_BYTE;
                args.count = args.data_size / typelen;
            }
            tcclSmallSizeAllreduce(&args, comm, stream);
            goto OP_END;
        }

        args.sendList = comm->devList;
        SDAACHECK(tcclStreamWriteValue64(stream, &args.sendList[rank_id], (uint64_t)sendusr, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        args.sendnum = dev_num;

        args.recvList = comm->devList + DEV_NUM;
        SDAACHECK(tcclStreamWriteValue64(stream, &args.recvList[rank_id], (uint64_t)recvusr, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        if (nranks == dev_num) {
            args.recvnum = dev_num;
        } else {
            args.recvnum = 1;
        }

        tcclDevSyncCard(comm, stream);

        if (nranks == dev_num) {
            args.dev_num = comm->dev_num;
            tcclDeviceAllReduce(&args);
            tcclDevSyncCard(comm, stream);
            goto OP_END;
        }
#ifdef _SWIB_
        tcclNicPublicWork(comm, &args);
#else
        tcclPublicWork(comm, &args);
#endif
    } else if (info->coll == tcclCollBroadcast) {
        const int rank_id = comm->node->rank_id;

        args.sendusr = (char*)sendusr;
        args.sendList = comm->devList;
        args.recvList = comm->devList + DEV_NUM;
        args.recvnum = dev_num;

        if (rank == args.bcastroot) SDAACHECK(tcclStreamWriteValue64(stream, &args.sendList[0], (uint64_t)sendusr, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        SDAACHECK(tcclStreamWriteValue64(stream, &args.recvList[rank_id], (uint64_t)recvusr, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        tcclDevSyncCard(comm, stream);

        tcclPublicWork(comm, &args);
    } else if (info->coll == tcclCollAllGather) {
        int rank_id = comm->node->rank_id;
        args.rank_id = rank_id;
        //broadcast sendusr and recvusr among card;
        args.sendList = comm->devList;
        SDAACHECK(tcclStreamWriteValue64(stream, &args.sendList[rank_id], (uint64_t)sendusr, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        args.sendnum = dev_num;
        args.recvList = comm->devList + DEV_NUM;
        SDAACHECK(tcclStreamWriteValue64(stream, &args.recvList[rank_id], (uint64_t)recvusr, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        if (nranks == dev_num) {
            args.recvnum = dev_num;
        } else {
            SDAACHECK(tcclStreamWriteValue64(stream, comm->tmp, (uint64_t)comm->addr.inBuff, SDAA_STREAM_WRITE_VALUE_DEFAULT));
            args.recvnum = 1;
        }
        tcclDevSyncCard(comm, stream);

        if (nranks == dev_num){
            const char *to_be_write = comm->addr.inBuff;   // for recive data from last card
            const size_t local_recvusr_off = rank * data_size;
            const int rank_num = comm->node->rank_num;
            tccl_move_to_list((void*)sendusr, comm->devList + DEV_NUM, rank_num, local_recvusr_off, data_size, stream, true);
            tcclDevSyncCard(comm, stream);
            return NULL;
        } else {
            //tccl_move_to_cross((void *)sendusr, (void*)comm->addr.outBuff, data_size, stream);
            args.sendusr = (char*)sendusr;
            tcclPublicWork(comm, &args);
        }
        //tcclDevSyncCard(comm, stream);
        //tccl_move_to_cross(cross_ad, recvusr, data_size * nranks, stream);
    } else if (info->coll == tcclCollReduceScatter) {
        int rank_id = comm->node->rank_id;
        args.nranks = nranks;
        args.rank_id = rank_id;
        args.dev_num = dev_num;
        args.devPhyRankList = comm->allRing->devPhyRankList;
        args.sendList = comm->devList;
        SDAACHECK(tcclStreamWriteValue64(stream, &args.sendList[rank_id], (uint64_t)sendusr, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        args.recvList = comm->devList + DEV_NUM;
        SDAACHECK(tcclStreamWriteValue64(stream, &args.recvList[rank_id], (uint64_t)recvusr, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        args.sendnum = dev_num;
        tcclDevSyncCard(comm, stream);

        if (nranks == dev_num) {
            args.recvnum = 1;
            args.s_offset = args.data_size * rank;
            args.r_offset = 0;
            args.sendOffsetNum = 1;
            tcclDeviceReduce(&args);
            tcclDevSyncCard(comm, stream);
            goto OP_END;
        } else {
            args.sendusr = (char*)sendusr;
            args.recvusr = (char*)recvusr;
            args.crossIn = comm->nic_info[rank].inBuff;
            args.crossOut = comm->nic_info[rank].outBuff;
            tcclPublicWork(comm, &args);
        }
    } else if (info->coll == tcclCollReduce) {
        int rank_id = comm->node->rank_id;
        args.nranks = nranks;
        args.rank_id = rank_id;
        args.dev_num = dev_num;
        args.root_rankid = comm->nic_info[args.bcastroot].rankId;
        args.sendList = comm->devList;
        SDAACHECK(tcclStreamWriteValue64(stream, &args.sendList[rank_id], (uint64_t)sendusr, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        args.recvList = comm->devList + DEV_NUM;
        SDAACHECK(tcclStreamWriteValue64(stream, &args.recvList[rank_id], (uint64_t)recvusr, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        args.sendnum = dev_num;
        tcclDevSyncCard(comm, stream);

        if (nranks == dev_num) {
            args.recvnum = 1;
            args.s_offset = 0;
            args.r_offset = 0;
            args.sendOffsetNum = 1;
            tcclDeviceReduce(&args);
            tcclDevSyncCard(comm, stream);
            goto OP_END;
        } else {
            args.sendusr = (char*)sendusr;
            args.recvusr = (char*)recvusr;
            args.crossIn = comm->nic_info[rank].inBuff;
            args.crossOut = comm->nic_info[rank].outBuff;
            tcclPublicWork(comm, &args);
        }
    } else if (info->coll == tcclCollSend) {
        int peer = info->root;
        int peer_rank_id = comm->nic_info[peer].rankId;
        tcclConnectType ctype = comm->connectType[peer];
        struct tcclTransportParam param = {0};
        void *src = comm->nic_info[rank].inBuff;
        void *dst = comm->nic_info[peer].outBuff;

        // 0. copy data: sendbuff --> sender's crossBuff
        if (ctype != TCCL_DMA) tcclTransportSend(comm, rank, info->sendbuf, src, data_size, stream, &param);
        // 1. waiting for notification from receiver
        tcclSendRecvWait(comm, peer, stream);
        // 2. send data: sender's crossBuff  --> receiver's crossBuff
        if (ctype == TCCL_DMA) {
            src = info->sendbuf;
            dst = (void*)(comm->devList + DEV_NUM + peer_rank_id);
            param.readDeviceRecv = true;
        }
        if (ctype == TCCL_IB) {
            // send data and flag in one HostFunc later
            param.ibArgs.type = SEND_BUFF;
            param.ibArgs.datasize = data_size;
            param.ibArgs.off = 0;
            tcclSendRecvNotify(comm, peer, stream, &param.ibArgs.flagoff);
        }
        if (ctype == TCCL_NIC) {
            src = comm->nic_info[rank].inPcie;
            dst = comm->nic_info[peer].outPcie;
        }
        tcclTransportSend(comm, peer, src, dst, data_size, stream, &param);
        // 3. notify receiver to receive data
        if (ctype != TCCL_IB) tcclSendRecvNotify(comm, peer, stream);
    } else if (info->coll == tcclCollRecv) {
        void *src = comm->nic_info[rank].outBuff;
        void *dst = info->recvbuf;
        int peer = info->root;
        struct tcclTransportParam param = {0};
        tcclConnectType ctype = comm->connectType[peer];
        if (ctype == TCCL_DMA) {
            void **recvList = comm->devList + DEV_NUM;
            SDAACHECK(tcclStreamWriteValue64(stream, &recvList[rank_id], (uint64_t)dst, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        }

        // 1. notify sender to send data
        tcclSendRecvNotify(comm, peer, stream);
        // 2. waiting for notification from sender
        tcclSendRecvWait(comm, peer, stream);
        // 3. copy data: receiver's crossBuff -->  recvbuff
        if (ctype != TCCL_DMA) tccl_move_to_cross_pro(src, dst, data_size, stream);
    } else if (info->coll == tcclCollAlltoall) {
        if (nranks == dev_num) {
            args.sendusr = (char*)sendusr;
            args.recvList = comm->devList + DEV_NUM_MAX;

            args.rank_id = rank_id;
            args.dev_num = dev_num;
            args.nranks = nranks;
            SDAACHECK(tcclStreamWriteValue64(stream, &args.recvList[rank_id], (uint64_t)recvusr, SDAA_STREAM_WRITE_VALUE_DEFAULT));
            tcclDevSyncCard(comm, stream);
            tcclDeviceAlltoall(&args);
            tcclDevSyncCard(comm, stream);
        } else {
            args.sendusr = (char*)sendusr;
            args.recvusr = (char*)recvusr;
            tcclPublicWork(comm, &args);
        }
    } else {
    }
OP_END:
    return NULL;
}//NOLINT

tcclResult_t tcclEnqueueCheck(struct tcclInfo *info) {
    tcclEnqueueCheck_func(info);
    return tcclSuccess;
}
