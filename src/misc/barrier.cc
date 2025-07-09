#include "comm.h"
#include "tccl.h"
#include "shm.h"
#include "nic.h"
#include "collectives.h"
#include "debug.h"

tcclResult_t tcclBarrierCard(tcclComm_t comm) {
    if (comm->node->rank_num > 1) {
        TCCLCHECK(tcclBarrierNode(comm));
    }
    return tcclSuccess;
}

tcclResult_t tcclBarrierAll(tcclComm_t comm) {
    if (TCCL_UNLIKELY(comm->proxy == NULL)) {
        return tcclInternalError;
    }
    struct tcclSocket *sendnext = comm->proxy->allNext;
    struct tcclSocket *recvprev = comm->proxy->allPrev;
    int nranks = comm->nranks;
    int data = 1;

    for (int i = 0; i < nranks - 1; i++) {
        TCCLCHECK(tcclSocketSend(sendnext, &data, sizeof(int)));
        TCCLCHECK(tcclSocketRecv(recvprev, &data, sizeof(int)));
    }
    return tcclSuccess;
}

tcclResult_t tcclBarrierNode(tcclComm_t comm) {
    int nranks_per_node = comm->node->rank_num * comm->node->card_num;
    int data = 1;
    struct tcclSocket *sendnext = comm->proxy->nodeNext;
    struct tcclSocket *recvprev = comm->proxy->nodePrev;

    for (int i = 0; i < nranks_per_node - 1; i++) {
        TCCLCHECK(tcclSocketSend(sendnext, &data, sizeof(int)));
        TCCLCHECK(tcclSocketRecv(recvprev, &data, sizeof(int)));
    }
    return tcclSuccess;
}

tcclResult_t tcclBarrierNotify(tcclComm_t comm, int peer, int src_off, int dst_off,
                               uint64_t value, sdaaStream_t stream, int* onlyWrite = NULL) {
    tcclConnectType ctype = comm->connectType[peer];
    int myrank = comm->rank;
    char *src_flag = (char *)((uintptr_t)&comm->nic_info[myrank].tmpBuff[0] + src_off);
    char *dst_flag = (char *)((uintptr_t)&comm->nic_info[peer].flag_devi[0] + dst_off);
    char *src_flag_pcie = (char *)((uintptr_t)&comm->nic_info[myrank].tmpPcie[0] + src_off);
    char *dst_flag_pcie = (char *)((uintptr_t)&comm->nic_info[peer].flag_pcie[0] + dst_off);

    void *saddr = src_flag;
    void *daddr = dst_flag;
    if (ctype == TCCL_NIC) {
        saddr = src_flag_pcie;
        daddr = dst_flag_pcie;
    }
    struct tcclTransportParam param = {0};
    if (ctype == TCCL_IB) {
        param.ibArgs.type = SEND_BUFF;
        param.ibArgs.datasize = 0; // only send flag;
        param.ibArgs.off = 0;
        param.ibArgs.flagoff = src_off;
    }

    if (ctype == TCCL_DMA) {
        SDAACHECK(tcclStreamWriteValue64(stream, dst_flag, value, SDAA_STREAM_WRITE_VALUE_DEFAULT));
    } else {
        SDAACHECK(tcclStreamWriteValue64(stream, src_flag, value, SDAA_STREAM_WRITE_VALUE_DEFAULT));
        if (onlyWrite == NULL) tcclTransportSend(comm, peer, saddr, daddr, sizeof(uint64_t), stream, &param);
    }
    return tcclSuccess;
}

tcclResult_t tcclBarrierWait(tcclComm_t comm, int off, uint64_t value, sdaaStream_t stream) {
    int myrank = comm->rank;
    char* flag_addr = (char *)((uintptr_t)&comm->nic_info[myrank].flag_devi[0] + off);
    SDAACHECK(tcclStreamWaitValue64(stream, flag_addr, value, SDAA_STREAM_WAIT_VALUE_EQ));
    return tcclSuccess;
}

tcclResult_t tcclSendRecvNotify(tcclComm_t comm, int peer, sdaaStream_t stream, int* onlyWrite) {
    int myrank = comm->rank;
    uint64_t value = ++comm->sync.sendcntall[peer];
    int off = NOTIFY_NUM * sizeof(char * ) + (myrank + comm->nranks * (value % SYNC_NUM)) * sizeof(uint64_t);
    if (onlyWrite != NULL) *onlyWrite = off;
    return tcclBarrierNotify(comm, peer, off, off, value, stream, onlyWrite);
}

tcclResult_t tcclSendRecvWait(tcclComm_t comm, int peer, sdaaStream_t stream) {
    uint64_t value = ++comm->sync.recvcntall[peer];
    int off = NOTIFY_NUM * sizeof(char * ) + (peer + comm->nranks * (value % SYNC_NUM)) * sizeof(uint64_t);
    return tcclBarrierWait(comm, off, value, stream);
}

tcclResult_t tcclDevSyncInit(tcclComm_t comm) {
    TCCLCHECK(tcclCalloc(&comm->sync.sendcntall, comm->nranks));
    TCCLCHECK(tcclCalloc(&comm->sync.recvcntall, comm->nranks));

    void *syncflag = NULL;
    void *splitFlag = NULL;
    int rank_id  = comm->node->rank_id;

    if (rank_id == 0) {
        SDAACHECK(sdaaMallocCross(&syncflag, SYNC_NUM * sizeof(uint64_t)));
        SDAACHECK(sdaaMemset(syncflag, 0, SYNC_NUM * sizeof(uint64_t)));
        comm->list[0] = syncflag;
        SDAACHECK(sdaaMallocCross(&splitFlag, SPLIT_FLAG_MAX * sizeof(uint64_t)));
        SDAACHECK(sdaaMemset(splitFlag, 0, SPLIT_FLAG_MAX * sizeof(uint64_t)));
        comm->list[1] = splitFlag;

        tcclBarrierCard(comm);
    } else {
        tcclBarrierCard(comm);
        syncflag = comm->list[0];
        splitFlag = comm->list[1];
    }
    comm->sync.syncnt = 0;
    comm->sync.syncflag = (uint64_t *)syncflag;
    comm->sync.splitCnt = 0;
    comm->sync.splitFlag = (uint64_t *)splitFlag;

    tcclBarrierCard(comm);

    int card_num = comm->node->card_num;
    int card_id = comm->node->card_id;
    int node_id = comm->node->node_id;
    int len = card_num * sizeof(struct tcclMemHandle);
    char name[TCCL_SHM_NAME_LEN];
    (void)snprintf(name, sizeof(name), "CardDevSync-%d", node_id);
    tcclMemHandle *handleList = (tcclMemHandle *)tcclShmAlloc(name, comm->port, len);

    if (handleList == NULL) {
        printf("mmap error!\n");
        return tcclSystemError;
    }
    tcclBarrierNode(comm);

    if (rank_id == 0) {
        SDAACHECK(sdaaMallocCross((void **)&comm->sync.nextcard, DEV_NUM * sizeof(uint64_t)));
        SDAACHECK(sdaaMemset(comm->sync.nextcard, 0, DEV_NUM * sizeof(uint64_t)));
        SDAACHECK(sdaaIpcGetMemHandle(&handleList[card_id].handle, comm->sync.nextcard));
        handleList[card_id].pid = comm->pid;
        handleList[card_id].ptr = comm->sync.nextcard;
    }
    tcclBarrierNode(comm);

    for (int i = 0; i < card_num; i++) {
        if (i == card_id) {
            if (comm->pid == handleList[card_id].pid) {
                comm->sync.sameProc = true;
            } else {
                comm->sync.sameProc = false;
            }
        } else {
            if (comm->pid == handleList[i].pid) {
                comm->sync.sameProcList[i] = true;
            } else {
                comm->sync.sameProcList[i] = false;
            }
        }
    }

    if (rank_id != 0) {
        if (!comm->sync.sameProc) {
            SDAACHECK(sdaaIpcOpenMemHandle((void **)&comm->sync.nextcard, handleList[card_id].handle, 0));
        } else {
            comm->sync.nextcard = (uint64_t*)handleList[card_id].ptr;
        }
    }

    for (int i = 0; i < card_num; i++) {
        if (i == card_id) {
            comm->sync.nextlist[i] = comm->sync.nextcard;
        } else {
            if (comm->sync.sameProcList[i]) {
                comm->sync.nextlist[i] = (uint64_t*)handleList[i].ptr;
            } else {
                SDAACHECK(
                    sdaaIpcOpenMemHandle((void**)&comm->sync.nextlist[i], handleList[i].handle, 0));
            }
        }
    }

    comm->sync.nextcnt = 0;
    comm->sync.prevcnt = 0;
    comm->sync.myBcastCnt   = 0;
    comm->sync.peerBcastCnt = 0;
    tcclBarrierNode(comm);

    if (rank_id == 0) {
        SDAACHECK(sdaaMallocCross((void **)&comm->sync.peercard, DEV_NUM * sizeof(uint64_t)));
        SDAACHECK(sdaaMemset(comm->sync.peercard, 0, DEV_NUM * sizeof(uint64_t)));
        SDAACHECK(sdaaIpcGetMemHandle(&handleList[card_id].handle, comm->sync.peercard));
        handleList[card_id].ptr = comm->sync.peercard;
    }
    tcclBarrierNode(comm);

    if (rank_id != 0) {
        if (!comm->sync.sameProc) {
            SDAACHECK(sdaaIpcOpenMemHandle((void**)&comm->sync.peercard, handleList[card_id].handle, 0));
        } else {
            comm->sync.peercard = (uint64_t*)handleList[card_id].ptr;
        }
    }

    for (int i = 0; i < card_num; i++) {
        if (i == card_id) {
            comm->sync.peerlist[i] = comm->sync.peercard;
        } else {
            if (comm->sync.sameProcList[i]) {
                comm->sync.peerlist[i] = (uint64_t*)handleList[i].ptr;
            } else {
                SDAACHECK(sdaaIpcOpenMemHandle((void**)&comm->sync.peerlist[i], handleList[i].handle, 0));
            }
        }
    }

    comm->sync.peercnt = 0;
    tcclBarrierNode(comm);

    // all2all sync
    if (rank_id == 0) {
        SDAACHECK(sdaaMallocCross((void **)&comm->sync.a2acard, MAX_CARD_NUM * DEV_NUM_MAX * sizeof(uint64_t)));
        SDAACHECK(sdaaMemset(comm->sync.a2acard, 0, MAX_CARD_NUM * DEV_NUM_MAX * sizeof(uint64_t)));
        SDAACHECK(sdaaIpcGetMemHandle(&handleList[card_id].handle, comm->sync.a2acard));
        handleList[card_id].ptr = comm->sync.a2acard;
    }
    tcclBarrierNode(comm);

    if (rank_id != 0) {
        if (!comm->sync.sameProc) {
            SDAACHECK(sdaaIpcOpenMemHandle((void**)&comm->sync.a2acard, handleList[card_id].handle, 0));
        } else {
            comm->sync.a2acard = (uint64_t*)handleList[card_id].ptr;
        }
    }

    for (int i = 0; i < card_num; i++) {
        if (i == card_id) {
            comm->sync.a2alist[i] = comm->sync.a2acard;
        } else {
            if (comm->sync.sameProcList[i]) {
                comm->sync.a2alist[i] = (uint64_t*)handleList[i].ptr;
            } else {
                SDAACHECK(sdaaIpcOpenMemHandle((void**)&comm->sync.a2alist[i], handleList[i].handle, 0));
            }
        }
    }

    comm->sync.a2acnt = 0;
    tcclBarrierNode(comm);

    SDAACHECK(sdaaMallocCross((void **)&comm->sync.cntflag, 8 * sizeof(uint64_t)));
    SDAACHECK(sdaaMemset((void *)comm->sync.cntflag, 0, 8 * sizeof(uint64_t)));
    tcclShmFree(handleList, len);
    return tcclSuccess;
}

tcclResult_t tcclDevSyncRoot(tcclComm_t comm, sdaaStream_t stream, GraphManager * graph_manager) {
    int rank_num = comm->node->rank_num;
    uint64_t count = comm->sync.syncnt;

    comm->sync.syncnt++;

    uint64_t *syncflag = &comm->sync.syncflag[count % SYNC_NUM];

    uint64_t flag = count / SYNC_NUM + 1;

    //SDAACHECK(tcclStreamWriteValue64(stream, syncflag, 1, SDAA_STREAM_WRITE_VALUE_ADD));
    if (comm->node->rank_id == 0) {
        if (TCCL_UNLIKELY(graph_manager == nullptr)) {
            TCCL_ERROR_LOG("Graph manager Should Not be Null");
            return tcclInternalError;
        }
        graph_manager->beginModifyGraph();
        tcclGraphMemOpsNode(SDAA_STREAM_MEM_OP_WAIT_VALUE64, syncflag, rank_num * flag, SDAA_STREAM_WAIT_VALUE_EQ);
    }
    return tcclSuccess;
}

tcclResult_t tcclDevSyncOther(tcclComm_t comm, sdaaStream_t stream,  GraphManager * graph_manager) {
    int rank_num = comm->node->rank_num;
    uint64_t count = comm->sync.syncnt;

    comm->sync.syncnt++;

    uint64_t *syncflag = &comm->sync.syncflag[count % SYNC_NUM];

    uint64_t flag = count / SYNC_NUM + 1;

    if (comm->node->rank_id == 0) {
        if (TCCL_UNLIKELY(graph_manager == nullptr)) {
            TCCL_ERROR_LOG("Graph manager Should Not be Null");
            return tcclInternalError;
        }
        tcclGraphMemOpsNode(SDAA_STREAM_MEM_OP_SET_VALUE64, syncflag, rank_num * flag, SDAA_STREAM_WRITE_VALUE_DEFAULT);
        if (TCCL_UNLIKELY(graph_manager->endModifyGraph() != tcclSuccess)) {
            TCCL_ERROR_LOG("all graph nodes should be modified!");
            return tcclInternalError;
        }
        tcclLaunchGraph(stream);
    } else {
        SDAACHECK(tcclStreamWaitValue64(stream, syncflag, rank_num * flag, SDAA_STREAM_WAIT_VALUE_EQ));
    }
    return tcclSuccess;
}

tcclResult_t tcclDevSyncCard(tcclComm_t comm, sdaaStream_t stream) {
    int rank_num = comm->node->rank_num;
    uint64_t count = comm->sync.syncnt;

    comm->sync.syncnt++;

    uint64_t *syncflag = &comm->sync.syncflag[count % SYNC_NUM];

    uint64_t flag = count / SYNC_NUM + 1;

    SDAACHECK(tcclStreamWriteValue64(stream, syncflag, 1, SDAA_STREAM_WRITE_VALUE_ADD));
    SDAACHECK(tcclStreamWaitValue64(stream, syncflag, rank_num * flag, SDAA_STREAM_WAIT_VALUE_EQ));
    return tcclSuccess;
}

tcclResult_t tcclDevSyncToNextCard(tcclComm_t comm, sdaaStream_t stream) {
    uint64_t flag = ++comm->sync.nextcnt;
    int rank_id = comm->node->rank_id;
    int card_id = comm->node->card_id;
    int card_num = comm->node->card_num;
    void *cntflag = comm->sync.cntflag;
    int next_card = (card_id + 1) % card_num;

    SDAACHECK(tcclStreamWriteValue64(stream, cntflag, flag, SDAA_STREAM_WRITE_VALUE_DEFAULT));
    SDAACHECK(tcclIpcMemcpyPeertoPeerAsync(&comm->sync.nextlist[next_card][rank_id], cntflag, sizeof(uint64_t), SDAA_IPC_P2P_RO_CLOSE, stream));
    SDAACHECK(tcclStreamWaitValue64(stream, &comm->sync.nextlist[card_id][rank_id], flag, SDAA_STREAM_WAIT_VALUE_GEQ));
    return tcclSuccess;
}

tcclResult_t tcclDevSyncToPrevCard(tcclComm_t comm, sdaaStream_t stream) {
    tcclRankSendFlag(1, ++comm->sync.prevcnt, comm->allRing->phyPrevRank, comm, stream);
    tcclRankWaitFlag(1, comm->sync.prevcnt, comm, stream);
    return tcclSuccess;
}

/* Prev sync by logic card rank, using no.2 flag */
tcclResult_t tcclDevSyncToLgcPrevCard(tcclComm_t comm, sdaaStream_t stream) {
    tcclRankSendFlag(2, ++comm->sync.prevcnt, comm->allRing->lgcPrevRank, comm, stream);
    tcclRankWaitFlag(2, comm->sync.prevcnt, comm, stream);
    return tcclSuccess;
}
