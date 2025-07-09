#include "comm.h"
#include "debug.h"

#define BCAST_FLAG_START_NO 100
#define TCCL_P2PRECV_LAG_DEV_OFFSET 200

/* Description: host func for IB, use IB function transfer data & flag */
void tcclPostIbFunc(void *args) {
    struct tcclIbRingArgs *data = (struct tcclIbRingArgs *)args;

    BUFF_TYPE type = data->type;
    size_t datasize = data->datasize;
    int off = data->off;
    int peer = data->peer;
    int flagoff = data->flagoff;
    tcclComm_t comm = data->comm;
    struct ibvResources *res;
    struct ibvResources *flag = &comm->allRank.flagpair[peer]->send;
    pthread_mutex_t *ibHostLock = comm->allRank.ibHostLock;

    /* select data buff by type */
    if (type == SEND_BUFF) {
        res = &comm->allRank.sendpair[peer]->send;
    } else {
        res = &comm->allRank.recvpair[peer]->send;
    }

    if (ibHostLock) pthread_mutex_lock(ibHostLock);
    /* send data to remote, if datasize is 0 can skip the step */
    if (datasize != 0) {
        post_write(res, off, datasize);
        poll_completion(res, 1);
    }

    /* send flag to remote, if flagoff is -1 can skip the step */
    if (flagoff != -1) {
        post_write(flag, flagoff, sizeof(uint64_t));
        poll_completion(flag, 1);
    }
    if (ibHostLock) pthread_mutex_unlock(ibHostLock);

    free(data);
    return;
}

tcclResult_t tcclRankSendPostNoWait(int off, BUFF_TYPE type, size_t sizeBytes, int peer, tcclComm_t comm,
                                    sdaaStream_t stream, bool isSendFlag) {
    int rank = comm->rank;
    tcclConnectType connectType = comm->connectType[peer];
    char *dst = comm->nic_info[peer].outBuff + off;
    char *src = NULL;
    if (type == SEND_BUFF) src = comm->nic_info[rank].inBuff + off;
    if (type == RECV_BUFF) src = comm->nic_info[rank].outBuff + off;

    if (isSendFlag) ++comm->sync.nextcnt;

    uint64_t flag = comm->sync.nextcnt;

    char *flag_send_devi = (char *)&comm->nic_info[rank].tmpBuff[0];
    char *flag_recv_devi = (char *)&comm->nic_info[peer].flag_devi[0];

    if (connectType == TCCL_IB) {
        if (isSendFlag) {
            SDAACHECK(tcclStreamWriteValue64(stream, flag_send_devi, flag, SDAA_STREAM_WRITE_VALUE_DEFAULT));
            TCCL_IB_LAUNCH_HOST(comm, type, sizeBytes, off, peer, 0);
        } else {
            TCCL_IB_LAUNCH_HOST(comm, type, sizeBytes, off, peer, -1);
        }
    } else if (connectType == TCCL_P2P) {
        if (isSendFlag) SDAACHECK(tcclStreamWriteValue64(stream, flag_send_devi, flag, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        SDAACHECK(tcclIpcMemcpyPeertoPeerAsync(dst, src, sizeBytes, SDAA_IPC_P2P_RO_OPEN, stream));
        if (isSendFlag) SDAACHECK(tcclIpcMemcpyPeertoPeerAsync((void *)flag_recv_devi, (void *)flag_send_devi, sizeof(uint64_t), SDAA_IPC_P2P_RO_CLOSE, stream));
    } else if (connectType == TCCL_NIC) {
        // Use rdma when connected by NIC.
        if (isSendFlag) SDAACHECK(tcclStreamWriteValue64(stream, flag_send_devi, flag, SDAA_STREAM_WRITE_VALUE_DEFAULT));

        dst = comm->nic_info[peer].outPcie + off;
        if (type == SEND_BUFF) src = comm->nic_info[rank].inPcie + off;
        if (type == RECV_BUFF) src = comm->nic_info[rank].outPcie + off;
        char *fsp = (char *)&comm->nic_info[rank].tmpPcie[0];    // flag_send_pcie
        char *frp = (char *)&comm->nic_info[peer].flag_pcie[0];  // flag_recv_pcie

        // Send data+flag
        SDAACHECK(tcclNicRdmaWriteAsync(stream, comm->res->rdma_queue, comm->nic_info[peer].guid, (uint64_t)src,
                                        (uint64_t)dst, sizeBytes));
        if (isSendFlag) SDAACHECK(tcclNicRdmaWriteAsync(stream, comm->res->rdma_queue, comm->nic_info[peer].guid, (uint64_t)fsp, (uint64_t)frp, sizeof(uint64_t)));
    } else if (connectType == TCCL_DMA) {
        tccl_move_to_cross(src, dst, sizeBytes, stream);
        if (isSendFlag) SDAACHECK(tcclStreamWriteValue64(stream, flag_recv_devi, flag, SDAA_STREAM_WRITE_VALUE_DEFAULT));
    } else {
    }
    return tcclSuccess;
}

tcclResult_t tcclRankSendPost(int off, BUFF_TYPE type, size_t sizeBytes, int peer, tcclComm_t comm,
                              sdaaStream_t stream) {
    char *flag_wait_devi = (char *)&comm->nic_info[comm->rank].flag_devi[0];
    tcclRankSendPostNoWait(off, type, sizeBytes, peer, comm, stream);
    SDAACHECK(tcclStreamWaitValue64(stream, flag_wait_devi, comm->sync.nextcnt, SDAA_STREAM_WAIT_VALUE_GEQ));

    return tcclSuccess;
}

/**
 * tcclRankSendPostNoWait
 * pull infos from peer rank's outbuff
 * copy to current rank's inbuff/outbuff
 * type:
 *  - SEND_BUFF -> inbuff
 *  - RECV_BUFF -> outbuff
 */
tcclResult_t tcclRankRecvPostNoWait(int off, BUFF_TYPE type, size_t sizeBytes, int peer, tcclComm_t comm,
                                    sdaaStream_t stream) {
    int rank = comm->rank;
    uint64_t flag = ++comm->sync.nextcnt;
    char *flag_wait_devi = (char *)&comm->nic_info[rank].flag_devi[0 + TCCL_P2PRECV_LAG_DEV_OFFSET];
    char *flag_read_devi = (char *)&comm->nic_info[rank].flag_devi[1 + TCCL_P2PRECV_LAG_DEV_OFFSET];
    char *flag_peer_ready = (char *)&comm->nic_info[peer].flag_devi[1 + TCCL_P2PRECV_LAG_DEV_OFFSET];

    // recv message from peer rank
    char *flag_send_devi = (char *)&comm->nic_info[rank].tmpBuff[0];
    char *flag_recv_devi = (char *)&comm->nic_info[peer].flag_devi[0 + TCCL_P2PRECV_LAG_DEV_OFFSET];
    // dst is recv buff addr of current rank
    char *dst = comm->nic_info[rank].outBuff + off;
    // src is send buff addr of peer rank
    char *src = nullptr;
    if (type == SEND_BUFF) src = comm->nic_info[peer].inBuff + off;
    if (type == RECV_BUFF) src = comm->nic_info[peer].outBuff + off;

    // set sync flag
    SDAACHECK(tcclStreamWriteValue64(stream, flag_send_devi, flag, SDAA_STREAM_WRITE_VALUE_DEFAULT));

    SDAACHECK(
        tcclIpcMemcpyPeertoPeerAsync(flag_peer_ready, flag_send_devi, sizeof(uint64_t), SDAA_IPC_P2P_RO_CLOSE, stream));
    SDAACHECK(tcclStreamWaitValue64(stream, flag_read_devi, flag, SDAA_STREAM_WAIT_VALUE_EQ));

    // recv message from peer rank
    SDAACHECK(tcclIpcMemcpyPeertoPeerAsync(dst, src, sizeBytes, SDAA_IPC_P2P_RO_OPEN, stream));

    // send sync flag to peer rank
    SDAACHECK(tcclIpcMemcpyPeertoPeerAsync((void *)flag_recv_devi, (void *)flag_send_devi, sizeof(uint64_t),
                                           SDAA_IPC_P2P_RO_CLOSE, stream));
    return tcclSuccess;
}

/**
 * tcclRankSendPost
 * pull infos from peer rank's outbuff to current rank's inbuff/outbuff
 * type:
 *  - SEND_BUFF -> inbuff
 *  - RECV_BUFF -> outbuff
 */
tcclResult_t tcclRankRecvPost(int off, BUFF_TYPE type, size_t sizeBytes, int peer, tcclComm_t comm,
                              sdaaStream_t stream){
    char *flag_wait_devi = (char *)&comm->nic_info[comm->rank].flag_devi[0 + TCCL_P2PRECV_LAG_DEV_OFFSET];
    tcclRankRecvPostNoWait(off, type, sizeBytes, peer, comm, stream);
    // wait flag for transport sync
    SDAACHECK(tcclStreamWaitValue64(stream, flag_wait_devi, comm->sync.nextcnt, SDAA_STREAM_WAIT_VALUE_GEQ));
    return tcclSuccess;
}

tcclResult_t tcclRankSendFlag(int no, int flag, int peer, tcclComm_t comm, sdaaStream_t stream) {
    int rank = comm->rank;
    tcclConnectType connectType = comm->connectType[peer];

    char *flag_send_devi = (char *)&comm->nic_info[rank].tmpBuff[no];
    char *flag_recv_devi = (char *)&comm->nic_info[peer].flag_devi[no];

    SDAACHECK(tcclStreamWriteValue64(stream, flag_send_devi, flag, SDAA_STREAM_WRITE_VALUE_DEFAULT));
    if (connectType == TCCL_IB) {
        TCCL_IB_LAUNCH_HOST(comm, BUFF_BUTT, 0, 0, peer, (no * (int)sizeof(char *)));
    } else if (connectType == TCCL_P2P) {
        SDAACHECK(tcclIpcMemcpyPeertoPeerAsync((void *)flag_recv_devi, (void *)flag_send_devi, sizeof(uint64_t),
                                               SDAA_IPC_P2P_RO_CLOSE, stream));
    } else if (connectType == TCCL_NIC) {
        // Use rdma when connected by NIC.
        char *fsp = (char *)&comm->nic_info[rank].tmpPcie[no];    // flag_send_pcie
        char *frp = (char *)&comm->nic_info[peer].flag_pcie[no];  // flag_recv_pcie

        SDAACHECK(tcclNicRdmaWriteAsync(stream, comm->res->rdma_queue, comm->nic_info[peer].guid, (uint64_t)fsp,
                                        (uint64_t)frp, sizeof(uint64_t)));
    } else if (connectType == TCCL_DMA) {
        SDAACHECK(tcclStreamWriteValue64(stream, flag_recv_devi, flag, SDAA_STREAM_WRITE_VALUE_DEFAULT));
    } else {
    }
    return tcclSuccess;
}

tcclResult_t tcclRankWaitFlag(int no, int flag, tcclComm_t comm, sdaaStream_t stream) {
    char *flag_wait_devi = (char *)&comm->nic_info[comm->rank].flag_devi[no];
    SDAACHECK(tcclStreamWaitValue64(stream, flag_wait_devi, flag, SDAA_STREAM_WAIT_VALUE_GEQ));
    return tcclSuccess;
}

tcclResult_t tcclBcastSendAckFlag(int no, int value, tcclComm_t comm, sdaaStream_t stream) {
    if (comm->reuseCross) {
        const int rank = comm->rank;
        const int peer = comm->allRing->phyPrevRank;
        tcclConnectType connectType = comm->connectType[peer];

        int bfsn = BCAST_FLAG_START_NO;  // broadcast flag start no
        char *bcastAckSend = (char *)&comm->nic_info[rank].tmpBuff[no + bfsn];
        char *bcastAckRecv = (char *)&comm->nic_info[peer].flag_devi[no + bfsn];

        SDAACHECK(tcclStreamWriteValue64(stream, bcastAckSend, value, SDAA_STREAM_WRITE_VALUE_DEFAULT));

        if (connectType == TCCL_P2P) {
            SDAACHECK(tcclIpcMemcpyPeertoPeerAsync((void *)bcastAckRecv, (void *)bcastAckSend, sizeof(uint64_t),
                                                   SDAA_IPC_P2P_RO_CLOSE, stream));
        } else if (connectType == TCCL_IB) {
            TCCL_IB_LAUNCH_HOST(comm, BUFF_BUTT, 0, 0, peer, ((no + bfsn) * (int)sizeof(char *)));
        } else if (connectType == TCCL_NIC) {
            char *fsp = (char *)&comm->nic_info[rank].tmpPcie[no + bfsn];    // flag_send_pcie
            char *frp = (char *)&comm->nic_info[peer].flag_pcie[no + bfsn];  // flag_recv_pcie

            SDAACHECK(tcclNicRdmaWriteAsync(stream, comm->res->rdma_queue, comm->nic_info[peer].guid, (uint64_t)fsp,
                                            (uint64_t)frp, sizeof(uint64_t)));
        } else {
        }
    }
    return tcclSuccess;
}

tcclResult_t tcclBcastWaitAckFlag(int no, int stepNo, int nChunks, tcclComm_t comm, sdaaStream_t stream) {
    if (comm->reuseCross && stepNo > nChunks) {
        comm->sync.myBcastCnt++;
        int bfsn = BCAST_FLAG_START_NO;  // broadcast flag start no
        SDAACHECK(tcclStreamWaitValue64(stream, &comm->nic_info[comm->rank].flag_devi[no + bfsn], comm->sync.myBcastCnt,
                                        SDAA_STREAM_WAIT_VALUE_EQ));
    }
    return tcclSuccess;
}

tcclResult_t tcclTransportSend(tcclComm_t comm, int peer, void* src_addr, void* dst_addr, size_t data_size,
                               sdaaStream_t stream, struct tcclTransportParam *extra) {
    tcclConnectType ctype = comm->connectType[peer];

    switch (ctype) {
        case TCCL_DMA:
            tccl_move_to_cross(src_addr, dst_addr, data_size, stream, false, false, 0, extra->readDeviceRecv, 0);
            break;
        case TCCL_P2P:
            SDAACHECK(tcclIpcMemcpyPeertoPeerAsync(dst_addr, src_addr, data_size, SDAA_IPC_P2P_RO_OPEN, stream));
            break;
        case TCCL_IB:
            TCCL_IB_LAUNCH_HOST(comm, extra->ibArgs.type, extra->ibArgs.datasize, extra->ibArgs.off, peer, extra->ibArgs.flagoff);
            break;
        case TCCL_NIC:
            SDAACHECK(tcclNicRdmaWriteAsync(stream, comm->res->rdma_queue, comm->nic_info[peer].guid,
                                            (uint64_t)src_addr, (uint64_t)dst_addr, data_size));
            break;
        default:
            return tcclInternalError;
            break;
    }
    return tcclSuccess;
}
