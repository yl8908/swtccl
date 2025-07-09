#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include "alloc.h"
#include "argcheck.h"
#include "checks.h"
#include "collectives.h"
#include "proc.h"
#include "shm.h"
#include "sharp.h"
#include "graph.h"
#include "nic.h"
#include "debug.h"

#define PRINTF
std::unordered_map<IP_ADDR, IB_TABLE_ROW> IB_connect_table;
pthread_mutex_t ibtab_mutex = PTHREAD_MUTEX_INITIALIZER;

struct accept_element_t{
    IP_ADDR ip;
    int devid;
    tcclSocket* socket;
};

std::vector<accept_element_t> accepts;

struct tcclExtInfo {
    int rank;
    int nranks;
    union tcclSocketAddress extAddressListenRoot;
    union tcclSocketAddress extAddressListen;
};

struct tcclBootstrapRing {
    struct tcclSocket ringListen;
    struct tcclSocket ringRecv;
    struct tcclSocket ringSend;
    int rank;
    int nranks;
};

static union tcclSocketAddress bootstrapNetIfAddr;
static int tcclBootstrapNetInitDone = 0;
pthread_mutex_t tcclBootstrapNetLock = PTHREAD_MUTEX_INITIALIZER;

// 3 or 4 core-group, not support 3&4 on one machine
static int g_devnum = 4;

int tcclGetDevNum(void) {
    //printf("get dev num: %d\n", g_devnum);
    return g_devnum;
}

tcclResult_t tcclDevNumInit(void) {
    int dev_cnt;
    SDAACHECK(sdaaGetDeviceCount(&dev_cnt));
    g_devnum = dev_cnt/4;
    int last_phy_cid = -1, cur_phy_cid = -1;
    int i = 0;
    SDAACHECK(sdaaDeviceGetAttribute(&cur_phy_cid, sdaaDevAttrPciDeviceId, i));
    last_phy_cid = cur_phy_cid;
    for (i = 1; i < dev_cnt; i++) {
        SDAACHECK(sdaaDeviceGetAttribute(&cur_phy_cid, sdaaDevAttrPciDeviceId, i));
        if (cur_phy_cid != last_phy_cid) {
            g_devnum = i;
            return tcclSuccess;
        }
    }
    return tcclSuccess;
}

static tcclResult_t tcclBootstrapNetSend(struct tcclSocket *sock, void *data, int size) {
    TCCLCHECK(tcclSocketSend(sock, &size, sizeof(int)));
    TCCLCHECK(tcclSocketSend(sock, data, size));
    return tcclSuccess;
}

static tcclResult_t tcclBootstrapNetRecv(struct tcclSocket *sock, void *data, int size) {
    int recvSize;
    TCCLCHECK(tcclSocketRecv(sock, &recvSize, sizeof(int)));
    if (recvSize > size) {
        printf("Message truncated : received %d bytes instead of %d\n", recvSize, size);
        return tcclInternalError;
    }
    TCCLCHECK(tcclSocketRecv(sock, data, recvSize));
    return tcclSuccess;
}

static void tcclBootstrapRootShow(char *nstat, int rank, int n, int nranks) {
    int i;
    char buf[4096];
    int len = 0;
    int maxlen = sizeof(buf);
    int first = 1;
    int haspre = 0;

    memset(buf, 0, sizeof(buf));
    for (i = 0; (i < nranks) && (len < maxlen); i++) {
        if (0 != nstat[i]) {
            if (2 <= haspre) {
                len += snprintf(buf + len, maxlen - len, "-%d", i - 1);
            }
            haspre = 0;
            continue;
        }
        if (0 == haspre) {
            if (first) {
                len += snprintf(buf + len, maxlen - len, "%d", i);
                first = 0;
            } else {
                len += snprintf(buf + len, maxlen - len, ",%d", i);
            }
        } else if (i == (nranks - 1)) {
            len += snprintf(buf + len, maxlen - len, "-%d", i);
        }
        haspre += 1;
    }
    printf("rank %d jion, %d/%d ranks still not join [%s]\n", rank, nranks - n, nranks, buf);
    return;
}

static void *tcclBootstrapRoot(void *args) {
    struct tcclSocket *listenSock = (struct tcclSocket *)args;
    int nranks = 0, c = 0;
    char *nstat = NULL;
    int debug = 0;
    struct tcclExtInfo info;
    union tcclSocketAddress *rankAddresses = NULL;
    union tcclSocketAddress *rankAddressesRoot = NULL; // for initial rank <-> root information exchange
    union tcclSocketAddress *zero = NULL;
    TCCLCHECKGOTO(tcclCalloc(&zero, 1));

    do {
        struct tcclSocket sock;
        TCCLCHECKGOTO(tcclSocketInit(&sock, NULL, NULL, 0));
        TCCLCHECKGOTO(tcclSocketAccept(&sock, listenSock));
        TCCLCHECKGOTO(tcclBootstrapNetRecv(&sock, &info, sizeof(info)));
        close(sock.fd);

        if (c == 0) {
            nranks = info.nranks;
            TCCLCHECKGOTO(tcclCalloc(&rankAddresses, nranks));
            TCCLCHECKGOTO(tcclCalloc(&rankAddressesRoot, nranks));
            const char *debugstr = getenv("TCCL_DEBUG_INIT");
            debug = debugstr ? atoi(debugstr) : 0;
            if (1 == debug)
                TCCLCHECKGOTO(tcclCalloc(&nstat, nranks));
        }

        if (nranks != info.nranks) {
            printf("Bootstrap Root : mismatch in rank count from procs %d : %d\n", nranks, info.nranks);
            goto out;
        }

        if (memcmp(zero, &rankAddressesRoot[info.rank], sizeof(union tcclSocketAddress)) != 0) {
            printf("Bootstrap Root : rank %d of %d ranks has already checked in\n", info.rank, nranks);
            goto out;
        }

        memcpy(rankAddressesRoot+info.rank, &info.extAddressListenRoot, sizeof(union tcclSocketAddress));
        memcpy(rankAddresses+info.rank, &info.extAddressListen, sizeof(union tcclSocketAddress));
        ++c;
        if (1 == debug) {
            nstat[info.rank] = 1;
            tcclBootstrapRootShow(nstat, info.rank, c, nranks);
        }
    } while (c < nranks);

    for (int r = 0; r < nranks; ++r) {
        int next = (r + 1) % nranks;
        struct tcclSocket sock;
        sock.abortFlag = NULL;
        sock.asyncFlag = 0;

        memcpy(&sock.addr, rankAddressesRoot + r, sizeof(union tcclSocketAddress));
        TCCLCHECKGOTO(tcclSocketConnect(&sock));
        TCCLCHECKGOTO(tcclBootstrapNetSend(&sock, rankAddresses+next, sizeof(union tcclSocketAddress)));
        close(sock.fd);
    }

out:
    if (nstat) free(nstat);
    close(listenSock->fd);
    free(listenSock);
    if (rankAddresses) free(rankAddresses);
    if (rankAddressesRoot) free(rankAddressesRoot);
    if (zero) free(zero);

    return NULL;
}

tcclResult_t tcclBootstrapCreateRoot(tcclUniqueId *id) {
    struct tcclSocket *listenSock = NULL;
    TCCLCHECK(tcclCalloc(&listenSock, 1));
    memcpy(&listenSock->addr, id, sizeof(union tcclSocketAddress));

    TCCLCHECKGOTO(tcclSocketListen(listenSock));

    memcpy(id, &listenSock->addr, sizeof(union tcclSocketAddress));
    pthread_t thread;
    pthread_create(&thread, NULL, tcclBootstrapRoot, (void *)listenSock);
    pthread_detach(thread); // will not be pthread_join()'d
    return tcclSuccess;
out:
    tcclFree(listenSock);
    return tcclSystemError;
}

tcclResult_t tcclBootstrapNetInit(void) {
    static char bootstrapNetIfName[MAX_IF_NAME_SIZE+1];
    if (tcclBootstrapNetInitDone == 0) {
        pthread_mutex_lock(&tcclBootstrapNetLock);
        if (tcclBootstrapNetInitDone == 0) {
            int nIfs = tcclFindInterfaces(bootstrapNetIfName, &bootstrapNetIfAddr, MAX_IF_NAME_SIZE, 1);
            if (nIfs <= 0) {
                pthread_mutex_unlock(&tcclBootstrapNetLock);
                printf("Bootstrap : no socket interface found\n");
                return tcclInternalError;
            }
            tcclBootstrapNetInitDone = 1;
        }
        pthread_mutex_unlock(&tcclBootstrapNetLock);
    }
    return tcclSuccess;
}

tcclResult_t tcclBootstrapGetUniqueId(tcclUniqueId *id) {
    memset(id, 0, sizeof(tcclUniqueId));
    memcpy(id, &bootstrapNetIfAddr, sizeof(union tcclSocketAddress));
    TCCLCHECK(tcclBootstrapCreateRoot(id));
    return tcclSuccess;
}

tcclResult_t tcclBootstrapAllGather(struct tcclBootstrapRing *ring, void *allData, int size) {
    char *data = (char *)allData;
    int rank = ring->rank;
    int nranks = ring->nranks;

    TRACE(TCCL_INIT, "rank %d nranks %d size %d", rank, nranks, size);

    /* Simple ring based AllGather
    * At each step i receive data from (rank-i-1) from left
    * and send previous step's data from (rank-i) to right
    */
    for (int i = 0; i < nranks - 1; i++) {
        size_t rslice = (rank - i - 1 + nranks) % nranks;
        size_t sslice = (rank - i + nranks) % nranks;

        // Send slice to the right
        TCCLCHECK(tcclBootstrapNetSend(&ring->ringSend, data + sslice * size, size));
        // Recv slice from the left
        TCCLCHECK(tcclBootstrapNetRecv(&ring->ringRecv, data + rslice * size, size));
    }

    TRACE(TCCL_INIT, "rank %d nranks %d size %d - DONE", rank, nranks, size);
    return tcclSuccess;
}

tcclResult_t tcclProxyAllGather(tcclComm_t comm, void *allData, int size) {
    char *data = (char *)allData;
    int nranks = comm->nranks;
    int rank = comm->rank;
    struct tcclSocket *sendnext = comm->proxy->allNext;
    struct tcclSocket *recvprev = comm->proxy->allPrev;

    TRACE(TCCL_INIT, "rank %d nranks %d size %d", rank, nranks, size);

    /* Simple ring based AllGather
    * At each step i receive data from (rank-i-1) from left
    * and send previous step's data from (rank-i) to right
    */
    for (int i = 0; i < nranks - 1; i++) {
        size_t rslice = (rank - i - 1 + nranks) % nranks;
        size_t sslice = (rank - i + nranks) % nranks;

        // Send slice to the right
        TCCLCHECK(tcclSocketSend(sendnext, data + sslice * size, size));
        // Recv slice from the left
        TCCLCHECK(tcclSocketRecv(recvprev, data + rslice * size, size));
    }

    TRACE(TCCL_INIT, "rank %d nranks %d size %d - DONE", rank, nranks, size);
    return tcclSuccess;
}

bool tcclIsOneCard(struct tcclDevid *devA, struct tcclDevid *devB) {
    return ((devA->ipaddr == devB->ipaddr) && ((devA->devid / DEV_NUM) == (devB->devid / DEV_NUM)));
}

bool tcclIsInTheCard(int rankList[], int rank) {
    bool isIn = false;
    for (int i = 0; i < g_devnum; i ++) {
        if (rankList[i] == rank) {
            isIn = true;
            break;
        }
    }
    return isIn;
}


bool tcclIsInTheNode(tcclComm_t comm, int peer_rank) {
#ifdef _SWIB_
    // Determin whether peer's nodeId equals self's nodeId.
    return comm->nic_info[peer_rank].nodeId == comm->node->node_id;
#endif

    if (tcclIsInTheCard(comm->node->rankList, peer_rank)) return true;
    if (comm->node->card_num == 1) return false;

    int peer_card_id = -1;
    int card_num = comm->node->card_num;
    tcclGetPeerId(comm, peer_rank, &peer_card_id, NULL);
    if (peer_card_id >= 0 && peer_card_id < card_num) return true;
    return false;
}

void tcclGetPeerId(tcclComm_t comm, int peer_rank, int *peer_card_id, int *peer_rank_id) {
#ifdef _SWIB_
    if (peer_card_id != NULL) *peer_card_id = comm->nic_info[peer_rank].cardId;
    if (peer_rank_id != NULL) *peer_rank_id = comm->nic_info[peer_rank].rankId;
#else
    int card_num = comm->node->card_num;

    if (card_num == 1) {
        int card_id = comm->node->card_id;
        int *rankList = comm->node->rankList;
        for (int i = 0; i < DEV_NUM; i++) {
            if (rankList[i] == peer_rank) {
                if (peer_card_id != NULL) *peer_card_id = card_id;
                if (peer_rank_id != NULL) *peer_rank_id = i;
                break;
            }
        }
        return;
    }

    bool flag = false;
    for (int i = 0; i < card_num; i++) {
        int *rankList = comm->proxy->cardrank[i].rankList;
        for (int j = 0; j < DEV_NUM; j++) {
            if (rankList[j] == peer_rank) {
                if (peer_card_id != NULL) *peer_card_id = i;
                if (peer_rank_id != NULL) *peer_rank_id = j;
                flag = true;
                break;
            }
        }
        if (flag) break;
    }
#endif
}

/* ------------------------------------------------------------- */
tcclResult_t tcclNicRankList(tcclComm_t comm) {
    int rank = comm->rank;
    int nranks = comm->nranks;
    struct tcclDevid *dev = comm->proxy->dev;
    struct tcclDevid *devtmp = NULL;
    struct tcclNode *node = NULL;
    struct tcclNetRingInfo *netRing = NULL;
    struct tcclAllRingInfo *allRing = NULL;
    struct tcclCardRankList *cardrank = NULL;
    int ret = rank;
    int k = 0;

    int rankList[nranks][DEV_NUM];
    int rank_num[nranks] = { 0 };
    int card_id[nranks] = { 0 };
    int card_num[nranks] = { 0 };
    int node_id[nranks] = { 0 };
    int node_num;
    struct tcclCardRankList noderank[nranks] = { 0 };

    int nodecnt = 0;
    int rankListTmp[nranks][DEV_NUM];

#ifdef _SWIB_
    int nic_topo[8] = { 0, 1, 4, 5, 6, 7, 2, 3 };
#else
    int nic_topo[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
#endif
    int phy_card_id[nranks] = { 0 };
    memset(phy_card_id, 0xFF, nranks * sizeof(int));
    for (int i=0; i < nranks; i++) { phy_card_id[i] = dev[i].phyCardId; }

    TCCLCHECKGOTO(tcclCalloc(&devtmp, nranks));
    TCCLCHECKGOTO(tcclCalloc(&node, 1));
    TCCLCHECKGOTO(tcclCalloc(&netRing, 1));
    TCCLCHECKGOTO(tcclCalloc(&allRing, 1));

    memcpy(devtmp, dev, nranks * sizeof(struct tcclDevid));
    memset(rankList, 0xFF, nranks * DEV_NUM * sizeof(int));
    memset(card_id, 0xFF, nranks * sizeof(int));
    memset(node_id, 0xFF, nranks * sizeof(int));

    for (int i = 0; i < nranks; i++) {
        for (int j = 0; j < nranks; j++) {
            if (tcclIsOneCard(&dev[i], &devtmp[j]) == true) {
                rankList[i][rank_num[i]] = j;
                rank_num[i]++;
                devtmp[j].devid = -1;
                devtmp[j].ipaddr = -1;
            }
        }
    }

    memcpy(rankListTmp, rankList, nranks * DEV_NUM * sizeof(int));
    for (int i = 0; i < nranks; i++) {
#ifndef _NODE_TEST_
        if (rankListTmp[i][0] == -1) continue;
        int iptmp = dev[rankListTmp[i][0]].ipaddr;
        int cardcnt = 0;

        for (int idx = 0; idx < MAX_CARD_NUM; idx++){
            for (int j = 0; j < nranks; j++) {
                if (rankListTmp[j][0] == -1 || phy_card_id[j] != nic_topo[idx] ) continue;
                if (dev[rankListTmp[j][0]].ipaddr == iptmp) {
                    card_id[j] = cardcnt;
                    cardcnt++;
                }
            }
        }

        for (int j = 0; j < nranks; j++) {
            if (rankListTmp[j][0] == -1) continue;
            if (dev[rankListTmp[j][0]].ipaddr == iptmp) {
                card_num[j] = cardcnt;
                node_id[j] = nodecnt;
                memset(rankListTmp[j], 0xFF, DEV_NUM*sizeof(int));
            }
        }
        nodecnt++;
#else
        if (rankListTmp[i][0] == -1) continue;
        card_id[i] = 0;
        card_num[i] = 1;
        node_id[i] = nodecnt++;
#endif
    }
    node_num = nodecnt;

    for (int i = 0; i < nranks; i++) {
        if (tcclIsInTheCard(rankList[i], rank)) ret = i;
    }
    memcpy(node->rankList, rankList[ret], DEV_NUM * sizeof(int));
    node->rank_num = rank_num[ret];
    for (int i = 0; i < node->rank_num; i++) {
        if (node->rankList[i] == rank) node->rank_id = i;
    }
    node->card_id = card_id[ret];
    node->card_num = card_num[ret];
    node->node_id = node_id[ret];
    node->node_num = node_num;
    node->port = comm->port;
    node->devid = comm->devid;

    TCCLCHECKGOTO(tcclCalloc(&cardrank, node->card_num));
    for (int i = 0; i < nranks; i++) {
        if ((rankList[i][0] != -1) && (node_id[i] == node->node_id)) {
            memcpy(cardrank[k].rankList, rankList[i], DEV_NUM * sizeof(int));
            k++;
        }
    }
    k = 0;

    for (int i = 0; i < nodecnt; i++) {
        for (int j = 0; j < node->card_num; j++) {
            for (int x = 0; x < nranks; x++) {
                if (rankList[x][0] != -1 && node_id[x] == i && card_id[x] == j) {
                    memcpy(noderank[k].rankList, rankList[x], DEV_NUM * sizeof(int));
                    k++;
                }
            }
        }
    }

    comm->isOneNode = (node->node_num == 1 ? true : false);

    if (node->node_num > 1) {
        int rank_per_node = node->card_num * node->rank_num;
        comm->proxy->sharpnext = (rank + rank_per_node) % nranks;
        comm->proxy->sharpprev = (rank + nranks - rank_per_node) % nranks;
    }

    netRing->nextCardRank = cardrank[(node->card_id + 1) % node->card_num].rankList[node->rank_id];
    netRing->prevCardRank = cardrank[(node->card_id + node->card_num - 1) % node->card_num].rankList[node->rank_id];
    netRing->nextNodeRank = comm->proxy->sharpnext;
    netRing->prevNodeRank = comm->proxy->sharpprev;

    TCCLCHECKGOTO(tcclCalloc(&allRing->phyRankList, k));
    for (int j = 0; j < k; j++) {
        if (noderank[j].rankList[node->rank_id] == rank) {
            allRing->phyNextRank = noderank[(j + 1) % k].rankList[node->rank_id];
            allRing->phyPrevRank = noderank[(j + k - 1) % k].rankList[node->rank_id];
            allRing->phyCardRank = node->node_id * node->card_num + node->card_id;
            allRing->allCardNum = node->node_num * node->card_num;

            allRing->lgcNextRank = (rank + nranks + node->rank_num) % nranks;
            allRing->lgcPrevRank = (rank + nranks - node->rank_num) % nranks;
            allRing->lgcCardRank = rank / node->rank_num;
        }
        allRing->phyRankList[j] = noderank[j].rankList[node->rank_id];
    }

    PRINTF("(%2d->%2d) (%d-%2d) (%2d->%2d)\n", \
            rank, allRing->phyNextRank, node->node_id, node->card_id, \
            phy_card_id[rank], phy_card_id[allRing->phyNextRank]);

    PRINTF("R (%2d<-%2d->%2d ) CID (%2d )  PCID (%2d<-%2d->%2d )\n", \
            allRing->phyPrevRank, rank, allRing->phyNextRank, \
            allRing->phyCardRank, \
            phy_card_id[allRing->phyPrevRank], phy_card_id[rank], phy_card_id[allRing->phyNextRank]);

    PRINTF("rank:%d, phyNextRank:%d, phyPrevRank:%d, phyCardRank:%d, allCardNum:%d, logic next:%d, prev:%d, my:%d\n", \
            rank, allRing->phyNextRank, allRing->phyPrevRank, allRing->phyCardRank, allRing->allCardNum, allRing->lgcNextRank, allRing->lgcPrevRank, allRing->lgcCardRank);

    free(devtmp);
    comm->node = node;
    comm->dev_num = node->rank_num;
    comm->proxy->cardrank = cardrank;
    comm->netRing = netRing;
    comm->allRing = allRing;
    return tcclSuccess;
out:
    if (netRing) free(netRing);
    if (allRing) free(allRing);
    if (node) free(node);
    if (devtmp) free(devtmp);
    if (cardrank) free(cardrank);
    return tcclInvalidUsage;
}
/* ------------------------------------------------------------- */

tcclResult_t tcclProxyCreate(tcclComm_t comm) {
    int rank = comm->rank;
    int nranks = comm->nranks;
    struct tcclProxy *proxy = comm->proxy;
    struct tcclSocket *allNext = NULL;
    struct tcclSocket *allPrev = NULL;
    struct tcclSocket *nodeNext = NULL;
    struct tcclSocket *nodePrev = NULL;

    int next = (rank + 1) % nranks;
    int prev = (rank - 1 + nranks) % nranks;
    int node_id, nranks_per_node;

    TCCLCHECKGOTO(tcclCalloc(&allNext, 1));
    TCCLCHECKGOTO(tcclCalloc(&allPrev, 1));

    TCCLCHECKGOTO(tcclSocketInit(allNext, &proxy->proxyAddrList[next], comm->abortFlag, 0));
    TCCLCHECKGOTO(tcclSocketConnect(allNext));

    TCCLCHECKGOTO(tcclSocketAccept(allPrev, proxy->proxyListen));

    proxy->allNext = allNext;
    proxy->allPrev = allPrev;

    TCCLCHECK(tcclBarrierAll(comm));

    tcclNicRankList(comm);

    node_id = comm->node->node_id;
    nranks_per_node = comm->node->rank_num * comm->node->card_num;
    proxy->nodenext = ((rank + 1) % nranks_per_node) + node_id * nranks_per_node;
    proxy->nodeprev = ((rank + nranks_per_node - 1) % nranks_per_node) + node_id * nranks_per_node;

    proxy->nodeNext = allNext;
    proxy->nodePrev = allPrev;

    if (comm->node->node_num > 1) {
        TCCLCHECKGOTO(tcclCalloc(&nodeNext, 1));
        TCCLCHECKGOTO(tcclCalloc(&nodePrev, 1));

        TCCLCHECKGOTO(tcclSocketInit(nodeNext, &proxy->proxyAddrList[proxy->nodenext], comm->abortFlag, 0));
        TCCLCHECKGOTO(tcclSocketConnect(nodeNext));
        TCCLCHECKGOTO(tcclSocketAccept(nodePrev, proxy->proxyListen));

        proxy->nodeNext = nodeNext;
        proxy->nodePrev = nodePrev;
        TCCLCHECK(tcclBarrierNode(comm));

        // TODO ib&sharp init
        tcclNode *node = comm->node;
        tcclSharpPre(node);
        if (node->sharpEnable) {
            TCCLCHECK(tcclSharpIbvNameGet(node));
            struct tcclSocket *sharpNext;
            struct tcclSocket *sharpPrev;

            TCCLCHECK(tcclCalloc(&sharpNext, 1));
            TCCLCHECK(tcclCalloc(&sharpPrev, 1));
            TCCLCHECK(
                tcclSocketInit(sharpNext, &comm->proxy->proxyAddrList[comm->proxy->sharpnext], comm->abortFlag, 0));
            TCCLCHECK(tcclSocketConnect(sharpNext));
            TCCLCHECK(tcclSocketAccept(sharpPrev, comm->proxy->proxyListen));
            node->sharpNext = sharpNext;
            node->sharpPrev = sharpPrev;

            {
                int addr = node->sharpNext->addr.sin.sin_addr.s_addr;
                int port = node->sharpNext->addr.sin.sin_port;
            }
            {
                int addr = node->sharpPrev->addr.sin.sin_addr.s_addr;
                int port = node->sharpPrev->addr.sin.sin_port;
                addr = comm->proxy->proxyListen->addr.sin.sin_addr.s_addr;
                port = comm->proxy->proxyListen->addr.sin.sin_port;
            }
            TCCLCHECK(tcclSharpInit(node));
        }
    }
    return tcclSuccess;
out:
    if (allNext) tcclFree(allNext);
    if (allPrev) tcclFree(allPrev);
    if (nodeNext) tcclFree(nodeNext);
    if (nodePrev) tcclFree(nodePrev);
    return tcclSystemError;
}

#define IB_COPY(dst, src) do { \
    (dst).ib_ctx = (src).ib_ctx; \
    (dst).pd = (src).pd; \
    (dst).mr = (src).mr; \
} while (0)

inline struct tcclIbPair* get_element_from_table(IP_ADDR node_ip, int peer_devid, int type) {
    tcclIbPair* res;
    pthread_mutex_lock(&ibtab_mutex);
    res = type == 0 ? &IB_connect_table[node_ip][peer_devid].sendpair
                     : type == 1 ? &IB_connect_table[node_ip][peer_devid].recvpair
                                 : &IB_connect_table[node_ip][peer_devid].flagpair;
    pthread_mutex_unlock(&ibtab_mutex);
    return res;
}

tcclResult_t tcclIbInitUnconnectedPeer(tcclComm_t comm, const int peer){
    const IP_ADDR node_ip = comm->proxy->dev[peer].ipaddr;
    const int peer_devid = comm->proxy->dev[peer].devid;
    struct tcclIbPair *element = get_element_from_table(node_ip, peer_devid, 0/*check sendpair*/);
    if (element->initialized && comm->allRank.flagpair[peer] != NULL && comm->allRank.flagpair[peer]->initialized) {
        return tcclSuccess;
    }
    TCCLCHECK(tcclIbPairInitPeer3(comm, peer));
    return tcclSuccess;
}

/* Description: establish IB resource pairs whith other ranks, include send & recv res */
tcclResult_t tcclIbPairInitPeer(tcclComm_t comm, int peer, struct tcclIbPair **connect, tcclSocket *peerSockNet,
                                char *send_h, char *recv_h, int size, int pair_type) {
    int ret = 0;
    int copy_index = -1;
    int nranks = comm->nranks;
    int myrank = comm->rank;
    tcclDevid *comm_devs_info = comm->proxy->dev;
    int peer_ret;
    int peer_devid;
    struct cm_con_data_t *remoteSendQp = NULL;
    struct cm_con_data_t *remoteRecvQp = NULL;
    IP_ADDR node_ip;
    struct tcclIbPair *element = NULL;
    TCCLCHECKGOTO(tcclCalloc(&remoteSendQp, 1));
    TCCLCHECKGOTO(tcclCalloc(&remoteRecvQp, 1));
    node_ip = comm_devs_info[peer].ipaddr;
    peer_devid = comm_devs_info[peer].devid;
    if (pair_type != 2) {
        element = get_element_from_table(node_ip, peer_devid, pair_type);
    } else {
        TCCLCHECKGOTO(tcclCalloc(&element, 1));
    }
    connect[peer] = element;
    //connect[myrank] store the copyed_element
    if ((connect[myrank] != NULL) && (connect[myrank]->initialized)) {
        IB_COPY(element->send, connect[myrank]->send);
        IB_COPY(element->recv, connect[myrank]->recv);
    }
    element->send.size = size;
    element->recv.size = size;
    if (!element->initialized) {
        ret |= resources_create(&element->send, send_h, NULL);
        ret |= resources_create(&element->recv, recv_h, NULL);
        get_con_data(&element->send);
        get_con_data(&element->recv);
    }
    if (peerSockNet == NULL) {
        ret = -1;
        goto out;
    }

    TCCLCHECKGOTO(tcclSocketSend(peerSockNet, (void*)&element->send.local_props, sizeof(cm_con_data_t)));
    TCCLCHECKGOTO(tcclSocketRecv(peerSockNet, (void*)remoteSendQp, sizeof(cm_con_data_t)));
    TCCLCHECKGOTO(tcclSocketSend(peerSockNet, (void*)&element->recv.local_props, sizeof(cm_con_data_t)));
    TCCLCHECKGOTO(tcclSocketRecv(peerSockNet, (void*)remoteRecvQp, sizeof(cm_con_data_t)));

    if (!element->initialized) {
        element->remoteSendQps = *remoteSendQp;
        element->remoteRecvQps = *remoteRecvQp;
        ret |= connect_qp(&element->send, element->remoteRecvQps);
        ret |= connect_qp(&element->recv, element->remoteSendQps);
        element->initialized = true;
        if (connect[myrank] == NULL) {
            connect[myrank] = element;
        }
    }

    TCCLCHECKGOTO(tcclSocketSend(peerSockNet, (void*)&ret, sizeof(int)));
    TCCLCHECKGOTO(tcclSocketRecv(peerSockNet, (void*)&peer_ret, sizeof(int)));
    ret |= peer_ret;

    if (ret != 0) {
        fprintf(stderr, "[%d] IB pair connect to peer: %d failed !!\n", comm->rank, peer);
    }
out:
    if (remoteSendQp) tcclFree(remoteSendQp);
    if (remoteRecvQp) tcclFree(remoteRecvQp);
    if (ret != 0) {
        return tcclSystemError;
    }
    return tcclSuccess;
}

tcclResult_t tcclIbPairInitPeer3(tcclComm_t comm, int peer){
    struct tcclSocket *peerSockNet = NULL;
    const int flagLen = NOTIFY_NUM * sizeof(char *) + comm->nranks * sizeof(uint64_t) * 2;
    accept_element_t peerInfo = {0};
    IP_ADDR node_ip = comm->proxy->dev[peer].ipaddr;
    int peer_devid = comm->proxy->dev[peer].devid;

    if (comm->rank < peer) {
        TCCLCHECKGOTO(tcclCalloc(&peerSockNet, 1));
        TCCLCHECKGOTO(tcclSocketInit(peerSockNet, &comm->proxy->proxyAddrList[peer], comm->abortFlag, 0));
        TCCLCHECKGOTO(tcclSocketConnect(peerSockNet));
        peerInfo.devid = comm->proxy->dev[comm->rank].devid;
        peerInfo.ip = comm->proxy->dev[comm->rank].ipaddr;
        peerInfo.socket = NULL;
        TCCLCHECKGOTO(tcclSocketSend(peerSockNet, (void*)&peerInfo, sizeof(struct accept_element_t)));
    } else if (comm->rank > peer) {
        bool done = false;
        for (auto x = accepts.begin(); x < accepts.end();) {
            if ((x->ip == node_ip) && (x->devid == peer_devid)) {
                done = true;
                // pop socket;
                peerSockNet = x->socket;
                x = accepts.erase(x);
            }else{
                ++x;
            }
        }
        while (!done) {
            TCCLCHECKGOTO(tcclCalloc(&peerSockNet, 1));
            // accept one connection
            TCCLCHECKGOTO(tcclSocketAccept(peerSockNet, comm->proxy->proxyListen));
            // recv info data
            TCCLCHECKGOTO(tcclSocketRecv(peerSockNet, (void*)&peerInfo, sizeof(struct accept_element_t)));
            if ((peerInfo.ip == node_ip) && (peerInfo.devid == peer_devid)) {
                // is what i need
                done = true;
            } else {
                //push to vector
                peerInfo.socket = peerSockNet;
                accepts.push_back(peerInfo);
            }
        }
    } else {
        printf("self connect is unsupportted now!\n");
    }
    TCCLCHECKGOTO(tcclIbPairInitPeer(comm, peer, comm->allRank.sendpair, peerSockNet, comm->addr.inBuff_host, comm->addr.outBuff_host, MAX_SIZE, 0));
    TCCLCHECKGOTO(tcclIbPairInitPeer(comm, peer, comm->allRank.recvpair, peerSockNet, comm->addr.inBuff_host, comm->addr.outBuff_host, MAX_SIZE, 1));
    TCCLCHECKGOTO(tcclIbPairInitPeer(comm, peer, comm->allRank.flagpair, peerSockNet, comm->addr.tmpHost, comm->addr.flagHost, flagLen, 2));

out:
    if (peerSockNet) {
        close(peerSockNet->fd);
        TCCLCHECK(tcclFree(peerSockNet));
    }
    return tcclSuccess;
}

inline bool isPeerIbInRingOrTree(tcclComm_t comm, int i){
    bool res =  (!tcclIsInTheNode(comm, i)) && ((i == comm->allRing->phyNextRank) || (i == comm->allRing->phyPrevRank));
    return res;
}

/* Description: establish IB resource pairs whith other ranks, include send & recv res */
tcclResult_t tcclIbPairInit(tcclComm_t comm, tcclDevid* comm_devs_info, struct tcclIbPair **connect, char *send_h, char *recv_h, int size, int pair_type) {
    int ret = 0;
    int copy_index = -1;
    int nranks = comm->nranks;
    int myrank = comm->rank;
    int *rets = NULL;
    struct cm_con_data_t *remoteSendQps = NULL;
    struct cm_con_data_t *remoteRecvQps = NULL;
    IP_ADDR node_ip;
    int peer_devid;
    struct tcclIbPair *element = NULL;
    struct tcclIbPair *copy_element = NULL;

    TCCLCHECKGOTO(tcclCalloc(&rets, nranks));
    TCCLCHECKGOTO(tcclCalloc(&remoteSendQps, nranks * nranks));
    TCCLCHECKGOTO(tcclCalloc(&remoteRecvQps, nranks * nranks));

    for (int i = 0; i < nranks; i++) {
        if (!isPeerIbInRingOrTree(comm, i)) {
            continue;
        }
        node_ip = comm_devs_info[i].ipaddr;
        peer_devid = comm_devs_info[i].devid;
        //assert(peer_devid < MAX_DEV_NUM);
        if (pair_type != 2) {
            element = get_element_from_table(node_ip, peer_devid, pair_type);
        } else {  // flag pair, not reuse;
            TCCLCHECKGOTO(tcclCalloc(&element, 1));
        }
        connect[i] = element;
        if (copy_element != NULL) {
            IB_COPY(element->send, copy_element->send);
            IB_COPY(element->recv, copy_element->recv);
        }
        element->send.size = size;
        element->recv.size = size;
        if (!element->initialized) {
            ret |= resources_create(&element->send, send_h, NULL);
            ret |= resources_create(&element->recv, recv_h, NULL);
        }
        if (copy_element == NULL) {
            copy_element = element;
            connect[myrank] = copy_element;
        }
    }

    rets[myrank] = ret;
    tcclProxyAllGather(comm, (void *)rets, sizeof(int));
    for (int i = 0; i < nranks; i++) ret |= rets[i];

    if (ret == 0) {
        int data_size = sizeof(cm_con_data_t) * nranks;
        struct cm_con_data_t *sdata = remoteSendQps + myrank * nranks;
        struct cm_con_data_t *rdata = remoteRecvQps + myrank * nranks;

        for (int i = 0; i < nranks; i++) {
            if (!isPeerIbInRingOrTree(comm, i)) continue;
            element = connect[i];
            if (!element->initialized) {
                get_con_data(&element->send);
                get_con_data(&element->recv);
            }
            memcpy((void*)(sdata + i), (void*)&element->send.local_props, sizeof(cm_con_data_t));
            memcpy((void*)(rdata + i), (void*)&element->recv.local_props, sizeof(cm_con_data_t));
        }
        tcclProxyAllGather(comm, (void *)remoteSendQps, data_size);
        tcclProxyAllGather(comm, (void *)remoteRecvQps, data_size);
        for (int i = 0; i < nranks; i++) {
            if (!isPeerIbInRingOrTree(comm, i)) continue;
            element = connect[i];
            if (!element->initialized) {
                element->remoteSendQps = remoteSendQps[i*nranks + myrank];
                element->remoteRecvQps = remoteRecvQps[i*nranks + myrank];
                ret |= connect_qp(&element->send, element->remoteRecvQps);
                ret |= connect_qp(&element->recv, element->remoteSendQps);
                element->initialized = true;
            }
        }
        /* allgather ret of init, can be delete for optomize */
        rets[myrank] = ret;
        tcclProxyAllGather(comm, (void *)rets, sizeof(int));
        for (int i = 0; i < nranks; i++) ret |= rets[i];
    }

    if (ret != 0) {
        printf("all rank ib pair init failed\n");
        //TODO: clean res
    }
out:
    if (rets) tcclFree(rets);
    if (remoteSendQps) tcclFree(remoteSendQps);
    if (remoteRecvQps) tcclFree(remoteRecvQps);
    return tcclSuccess;
}

/* Description: create shm mutex for IB dev */
void tcclIbHostLockInit(tcclComm_t comm) {
    pthread_mutexattr_t ma;
    pthread_mutex_t *ibHostLock = NULL;
    char name[TCCL_SHM_NAME_LEN];
    int ibdev = tcclIBDevSelect(comm->devid, 32);

    (void)snprintf(name, sizeof(name), "Ib-lock-%d", ibdev);
    ibHostLock = (pthread_mutex_t *)tcclShmAlloc(name, comm->port, sizeof(pthread_mutex_t));
    if (ibHostLock == NULL) {
        return;
    }
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&ma, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(ibHostLock, &ma);

    comm->allRank.ibHostLock = ibHostLock;
    return;
}

/* Description: establish IB res to other node ranks, include data buff & flag buff */
tcclResult_t tcclAllRankIbInit(tcclComm_t comm, tcclDevid* comm_devs_info) {
    int nranks = comm->nranks;
    int rank_id = comm->node->rank_id;
    struct tcclIbPair **sendpair = NULL;
    struct tcclIbPair **recvpair = NULL;
    struct tcclIbPair **flagpair = NULL;
    char *sendbuf_h = comm->addr.inBuff_host;
    char *recvbuf_h = comm->addr.outBuff_host;
    char *flagsend  = comm->addr.tmpHost;
    char *flagrecv  = comm->addr.flagHost;
    int flagLen = NOTIFY_NUM * sizeof(char*) + comm->nranks * sizeof(uint64_t) * 2;

    TCCLCHECKGOTO(tcclCalloc(&sendpair, nranks));
    TCCLCHECKGOTO(tcclCalloc(&recvpair, nranks));
    TCCLCHECKGOTO(tcclCalloc(&flagpair, nranks));

    tcclIbPairInit(comm, comm_devs_info , sendpair, sendbuf_h, recvbuf_h, MAX_SIZE, 0);
    tcclIbPairInit(comm, comm_devs_info , recvpair, recvbuf_h, recvbuf_h, MAX_SIZE, 1);
    tcclIbPairInit(comm, comm_devs_info , flagpair, flagsend,  flagrecv,  flagLen,  2);

    comm->allRank.sendpair = sendpair;
    comm->allRank.recvpair = recvpair;
    comm->allRank.flagpair = flagpair;

    if (comm->node->card_num == 1) {
        tcclIbHostLockInit(comm);
    } else {
        comm->allRank.ibHostLock = NULL;
    }
    return tcclSuccess;
out:
    if (sendpair) tcclFree(sendpair);
    if (recvpair) tcclFree(recvpair);
    if (flagpair) tcclFree(flagpair);
    return tcclSystemError;
}

#define IB_CLEAR(ibpair) do {  \
    (ibpair)->send.ib_ctx = NULL;  \
    (ibpair)->send.pd = NULL;  \
    (ibpair)->send.mr = NULL;  \
    (ibpair)->recv.ib_ctx = NULL;  \
    (ibpair)->recv.pd = NULL;  \
    (ibpair)->recv.mr = NULL;  \
} while (0)

tcclResult_t tcclAllRankIbPairDestroy(struct tcclIbPair *pair) {
    resources_destroy(&pair->send);
    resources_destroy(&pair->recv);
    return tcclSuccess;
}

tcclResult_t tcclAllRankIbFini(tcclComm_t comm) {
    int nranks = comm->nranks;
    int rank_num = comm->node->rank_num;

    struct tcclIbPair** flag_connects = comm->allRank.flagpair;
    if (flag_connects[comm->rank] != NULL){ /* means at least one ib connects*/
        for (int i = 0; i < nranks; ++i) {
            if (tcclIsInTheNode(comm, i)) {
                continue;
            }
            if (flag_connects[i] != NULL) {
                if (flag_connects[i] == flag_connects[comm->rank]) continue;
                IB_CLEAR(flag_connects[i]);
                tcclAllRankIbPairDestroy(flag_connects[i]);
                tcclFree(flag_connects[i]);
            }
        }
        tcclAllRankIbPairDestroy(flag_connects[comm->rank]);
        flag_connects[comm->rank] = NULL;
        tcclFree(comm->allRank.flagpair);
    }

    if (comm->allRank.ibHostLock) tcclShmFree(comm->allRank.ibHostLock, sizeof(pthread_mutex_t));

    return tcclSuccess;
}

tcclResult_t tcclConnectTypeSet(tcclComm_t comm) {
    int rank = comm->rank;
    int nranks = comm->nranks;
    int myNodeId = comm->nic_info[rank].nodeId;
    int myPhyCardId = comm->nic_info[rank].phyCardId;

    TCCLCHECK(tcclCalloc(&comm->connectType, nranks));

    for (int i = 0; i < nranks; i++) {
        int peerNodeId = comm->nic_info[i].nodeId;
        int peerPhyCardId = comm->nic_info[i].phyCardId;
#ifdef _SWIB_
        if (myNodeId == peerNodeId && myPhyCardId == peerPhyCardId) {
            comm->connectType[i] = TCCL_DMA;
        } else if (myNodeId == peerNodeId && tcclNicIsSameSwitch(myPhyCardId, peerPhyCardId)) {
            comm->connectType[i] = TCCL_P2P;
        } else {
            comm->connectType[i] = TCCL_NIC;
        }

#else
        if (myNodeId != peerNodeId) {
            comm->connectType[i] = TCCL_IB;
        } else if (myPhyCardId == peerPhyCardId) {
            comm->connectType[i] = TCCL_DMA;
        } else {
            comm->connectType[i] = TCCL_P2P;
        }
#endif
    }

    return tcclSuccess;
}

tcclResult_t tcclRankMemOpen(tcclComm_t comm) {
    int rank = comm->rank;
    int nranks = comm->nranks;
    tcclNicInfo *nic_info = comm->nic_info;
    int selfPid = nic_info[rank].pid;

    for (int i = 0; i < nranks; i++) {
        int peerPid = nic_info[i].pid;
        if (tcclIsInTheNode(comm, i) && (i != rank) && (selfPid != peerPid)) {
            SDAACHECK(sdaaIpcOpenMemHandle((void **)&nic_info[i].inBuff,    nic_info[i].inHandle,    0));
            SDAACHECK(sdaaIpcOpenMemHandle((void **)&nic_info[i].outBuff,   nic_info[i].outHandle,   0));
            SDAACHECK(sdaaIpcOpenMemHandle((void **)&nic_info[i].tmpBuff,   nic_info[i].tmpHandle,   0));
            SDAACHECK(sdaaIpcOpenMemHandle((void **)&nic_info[i].flag_devi, nic_info[i].flagHandle,  0));
        }
    }
    return tcclSuccess;
}

tcclResult_t tcclRankMemClose(tcclComm_t comm) {
    int rank = comm->rank;
    int nranks = comm->nranks;
    tcclNicInfo *nic_info = comm->nic_info;
    int selfPid = nic_info[rank].pid;

    for (int i = 0; i < nranks; i++) {
        int peerPid = nic_info[i].pid;
        if (tcclIsInTheNode(comm, i) && (i != rank) && (selfPid != peerPid)) {
            SDAACHECK(sdaaIpcCloseMemHandle(nic_info[i].inBuff));
            SDAACHECK(sdaaIpcCloseMemHandle(nic_info[i].outBuff));
            SDAACHECK(sdaaIpcCloseMemHandle(nic_info[i].tmpBuff));
            SDAACHECK(sdaaIpcCloseMemHandle(nic_info[i].flag_devi));
        }
    }
    return tcclSuccess;
}

tcclResult_t  tcclBootstrapInit(tcclUniqueId *id, tcclComm_t comm) {
    int rank = comm->rank;
    int nranks = comm->nranks;
    int myPhyCardId = -1;
    struct tcclProxy *proxy = NULL;
    struct tcclSocket *proxyListen = NULL;
    union tcclSocketAddress *proxyAddrList = NULL;
    struct tcclDevid *dev = NULL;
    struct tcclNicInfo *nic_info = NULL;
    struct nic_res *res = NULL;

    TCCLCHECK(tcclCalloc(&proxy, 1));

    struct tcclBootstrapRing ring = { 0 };
    ring.rank = rank;
    ring.nranks = nranks;

    struct tcclExtInfo info = { 0 };
    info.rank = rank;
    info.nranks = nranks;

    struct tcclSocket sock, listenSockRoot;

    TCCLCHECKGOTO(tcclSocketInit(&sock, (union tcclSocketAddress *)id, comm->abortFlag, 0));
    TCCLCHECKGOTO(tcclSocketInit(&listenSockRoot, &bootstrapNetIfAddr, comm->abortFlag, 0));
    TCCLCHECKGOTO(tcclSocketInit(&ring.ringListen, &bootstrapNetIfAddr, comm->abortFlag, 0));
    TCCLCHECKGOTO(tcclSocketInit(&ring.ringSend, NULL, comm->abortFlag, 0));
    TCCLCHECKGOTO(tcclSocketInit(&ring.ringRecv, NULL, comm->abortFlag, 0));

    TCCLCHECKGOTO(tcclSocketListen(&ring.ringListen));
    TCCLCHECKGOTO(tcclSocketListen(&listenSockRoot));

    memcpy(&info.extAddressListen, &ring.ringListen.addr, sizeof(union tcclSocketAddress));
    memcpy(&info.extAddressListenRoot, &listenSockRoot.addr, sizeof(union tcclSocketAddress));

    TCCLCHECKGOTO(tcclSocketConnect(&sock));
    TCCLCHECKGOTO(tcclBootstrapNetSend(&sock, &info, sizeof(info)));
    close(sock.fd);

    TCCLCHECKGOTO(tcclSocketAccept(&sock, &listenSockRoot));
    TCCLCHECKGOTO(tcclBootstrapNetRecv(&sock, &ring.ringSend.addr, sizeof(union tcclSocketAddress)));
    close(sock.fd);
    close(listenSockRoot.fd);

    TCCLCHECKGOTO(tcclSocketConnect(&ring.ringSend));
    TCCLCHECKGOTO(tcclSocketAccept(&ring.ringRecv, &ring.ringListen));

    TCCLCHECKGOTO(tcclCalloc(&proxyListen, 1));
    TCCLCHECKGOTO(tcclSocketInit(proxyListen, &bootstrapNetIfAddr, NULL, 0));
    TCCLCHECKGOTO(tcclSocketListen(proxyListen));

    TCCLCHECKGOTO(tcclCalloc(&proxyAddrList, nranks));
    memcpy(proxyAddrList+rank, &proxyListen->addr, sizeof(union tcclSocketAddress));
    TCCLCHECKGOTO(tcclBootstrapAllGather(&ring, proxyAddrList, sizeof(union tcclSocketAddress)));

    SDAACHECK(sdaaDeviceGetAttribute(&myPhyCardId, sdaaDevAttrPciDeviceId, comm->devid));

    TCCLCHECKGOTO(tcclCalloc(&dev, nranks));
    dev[rank].rank = rank;
    dev[rank].devid = comm->devid;
    dev[rank].ipaddr = bootstrapNetIfAddr.sin.sin_addr.s_addr;
    dev[rank].phyCardId = myPhyCardId;
    TCCLCHECKGOTO(tcclBootstrapAllGather(&ring, dev, sizeof(struct tcclDevid)));

    proxy->proxyAddrList = proxyAddrList;
    proxy->proxyListen = proxyListen;
    proxy->dev = dev;
    comm->proxy = proxy;

    TCCLCHECKGOTO(tcclProxyCreate(comm));
    {
        int *devPhyRankList;
        int phyRankListLen = comm->allRing->allCardNum * sizeof(int);
        //SDAACHECK(sdaaMallocCross((void**)&devPhyRankList, phyRankListLen));//by-yl
        SDAACHECK(sdaaMallocCrossRegion((void**)&devPhyRankList, phyRankListLen));
        SDAACHECK(sdaaMemcpy((void *)devPhyRankList, (void *)comm->allRing->phyRankList, phyRankListLen, sdaaMemcpyHostToDevice));
        comm->allRing->devPhyRankList = devPhyRankList;
    }

    TCCLCHECKGOTO(tcclCalloc(&nic_info, nranks));
#ifdef _SWIB_
    if (comm->node->node_num == 1 && comm->node->card_num == 1) {
        res = (struct nic_res*)malloc(sizeof(struct nic_res));
    } else {
        tcclNicInit(&res, myPhyCardId);
    }
#endif
    comm->res = res;

    {
        auto& fd = nic_info[rank].flag_devi;
        auto& fp = nic_info[rank].flag_pcie;
        auto& tb = nic_info[rank].tmpBuff;
        auto& tp = nic_info[rank].tmpPcie;

        int flagLen = NOTIFY_NUM * sizeof(char *) + comm->nranks * sizeof(uint64_t) * 2;

        //SDAACHECK(sdaaMallocCross((void**)&fd, flagLen));
        SDAACHECK(sdaaMallocCrossRegion((void**)&fd, flagLen));
        //SDAACHECK(sdaaMallocCross((void**)&tb, flagLen));//by-yl
        SDAACHECK(sdaaMallocCrossRegion((void**)&tb, flagLen));

        SDAACHECK(sdaaMemset((void*)fd, 0, flagLen));
        SDAACHECK(sdaaMemset((void*)tb, 0, flagLen));

        SDAACHECK(sdaaCrossMemToBusMem((void*)fd, (uint64_t*)&fp));
        SDAACHECK(sdaaCrossMemToBusMem((void*)tb, (uint64_t*)&tp));

        SDAACHECK(sdaaIpcGetMemHandle(&nic_info[rank].tmpHandle,  tb));
        SDAACHECK(sdaaIpcGetMemHandle(&nic_info[rank].flagHandle, fd));

        /* get host addr for device mem, used by IB res */
        comm->addr.tmpBuff = (char *)tb;
        comm->addr.flagBuff = (char *)fd;
        SDAACHECK(sdaaMemDeviceGetHostPointer((void **)&comm->addr.tmpHost, tb));
        SDAACHECK(sdaaMemDeviceGetHostPointer((void **)&comm->addr.flagHost, fd));
        //PRINTF("malloc Notify : %p - %p - %p - %p - %p\n", fh, fd, fp, tb, tp);
    }

#ifdef _SWIB_
    nic_info[rank].guid        = comm->res->node_guid;
#endif
    nic_info[rank].phyCardId   = myPhyCardId;
    nic_info[rank].rankId      = comm->node->rank_id;
    nic_info[rank].cardId      = comm->allRing->phyCardRank;
    nic_info[rank].nodeId      = comm->node->node_id;
    nic_info[rank].inBuff      = comm->addr.inBuff;
    nic_info[rank].outBuff     = comm->addr.outBuff;
    nic_info[rank].inPcie      = comm->addr.inPcie;
    nic_info[rank].outPcie     = comm->addr.outPcie;
    nic_info[rank].inHandle    = comm->addr.inHandle;
    nic_info[rank].outHandle   = comm->addr.outHandle;
    nic_info[rank].nextRank    = comm->allRing->phyNextRank;
    nic_info[rank].phyCardRank = comm->allRing->phyCardRank;
    nic_info[rank].pid         = comm->pid;

    TCCLCHECKGOTO(tcclBootstrapAllGather(&ring, nic_info, sizeof(struct tcclNicInfo)));
    comm->nic_info = nic_info;

    if (rank == 0) {
        for (int i = 0; i < nranks; i++) {
            PRINTF("nic_info[%d] inPcie=%p outPcie=%p guid=%#lx\n", i, comm->nic_info[i].inPcie, nic_info[i].outPcie, nic_info[i].guid);
        }
    }
    tcclRankMemOpen(comm);
    // TODO add ib symbols init
    tcclAllRankIbInit(comm, dev);
    tcclConnectTypeSet(comm);

    close(ring.ringListen.fd);
    close(ring.ringSend.fd);
    close(ring.ringRecv.fd);

    return tcclSuccess;
out:
    if (proxy) tcclFree(proxy);
    if (proxyListen) tcclFree(proxyListen);
    if (dev) tcclFree(dev);
    if (proxyAddrList) tcclFree(proxyAddrList);
    if (nic_info) tcclFree(nic_info);
    return tcclSystemError;
}

tcclResult_t tcclBootstrapFini(tcclComm_t comm) {
    TCCLCHECK(tcclFree(comm->proxy->proxyAddrList));
    comm->proxy->proxyAddrList = NULL;

    SYSCHECK(close(comm->proxy->proxyListen->fd), "close");
    TCCLCHECK(tcclFree(comm->proxy->proxyListen));
    comm->proxy->proxyListen = NULL;

    SYSCHECK(close(comm->proxy->allNext->fd), "close");
    SYSCHECK(close(comm->proxy->allPrev->fd), "close");
    TCCLCHECK(tcclFree(comm->proxy->allNext));
    TCCLCHECK(tcclFree(comm->proxy->allPrev));
    comm->proxy->allNext = NULL;
    comm->proxy->allPrev = NULL;

    if (comm->node->node_num > 1) {
        SYSCHECK(close(comm->proxy->nodeNext->fd), "close");
        SYSCHECK(close(comm->proxy->nodePrev->fd), "close");
        TCCLCHECK(tcclFree(comm->proxy->nodeNext));
        TCCLCHECK(tcclFree(comm->proxy->nodePrev));
        comm->proxy->nodeNext = NULL;
        comm->proxy->nodePrev = NULL;
    }

    /* [haoz:] as IB resource managed by process, comm do not need to destory it */
    tcclAllRankIbFini(comm);

    if (comm->node != NULL) {
        TCCLCHECK(tcclSharpFini(comm->node));
        TCCLCHECK(tcclFree(comm->node));
        comm->node = NULL;
    }
    TCCLCHECK(tcclFree(comm->proxy));
    comm->proxy = NULL;
    return tcclSuccess;
}

static tcclResult_t tcclCommSocketInit(tcclComm_t comm, tcclUniqueId *commId) {
    TCCLCHECK(tcclBootstrapNetInit());
    TCCLCHECK(tcclCalloc(&comm->abortFlag, 1));
    *comm->abortFlag = 0;
    TCCLCHECK(tcclBootstrapInit(commId, comm));
    return tcclSuccess;
}

static tcclResult_t tcclCommSocketFini(tcclComm_t comm) {
    TCCLCHECK(tcclBootstrapFini(comm));
    TCCLCHECK(tcclFree(comm->abortFlag));
    return tcclSuccess;
}

static tcclResult_t tcclCommAlloc(tcclComm_t* comret, int nranks, int rank) {
    if (nranks < 1) {
        printf("invalid device count (%d) requested\n", nranks);
        return tcclInvalidArgument;
    }

    if (rank >= nranks || rank < 0) {
        printf("rank %d exceeds ndev=%d\n", rank, nranks);
        return tcclInvalidArgument;
    }

    tcclComm_t comm;
    TCCLCHECK(tcclCalloc(&comm, 1));

    comm->nranks = nranks;
    *comret = comm;

    return tcclSuccess;
}

tcclResult_t tcclDevListInit(tcclComm_t comm) {
    void *devList = NULL;
    int rank_id  = comm->node->rank_id;
    int len = (2 * DEV_NUM) * sizeof(void *);

    if (rank_id == 0) {
        //SDAACHECK(sdaaMallocCross(&devList, len));  //by-yl
        SDAACHECK(sdaaMallocCrossRegion(&devList, len));
        SDAACHECK(sdaaMemset(devList, 0, len));
        comm->list[0] = devList;
        tcclBarrierCard(comm);
    } else {
        tcclBarrierCard(comm);
        devList = comm->list[0];
    }
    comm->devList = (void **)devList;
    tcclBarrierCard(comm);
    return tcclSuccess;
}

/*Build Small Size allreduce graph*/
static tcclResult_t tcclGraphManagerInit(tcclComm_t comm) {
    // normal situation graph
    std::vector<tcclGraphNode_t> nodes;
    /*1. sync card and calculate */
    nodes.push_back(tcclGraphNode_t::MemOps);
    nodes.push_back(tcclGraphNode_t::Kernel);

    int dev_num = comm->dev_num;
    int card_num = comm->node->card_num;
    /*More than one phy card*/
    if (comm->nranks != dev_num) {
        /*2.Butterfly algorithm exchange data*/
        for (int stride = 1; stride < card_num; stride *= 2) {
            nodes.insert(nodes.end(), {
                tcclGraphNode_t::IpcP2p,
                tcclGraphNode_t::IpcP2p,
                tcclGraphNode_t::MemOps});
        }
        /*3. 0 node calculate and notify 1 2 3 finish*/
        nodes.push_back(tcclGraphNode_t::Kernel);
    }
    nodes.push_back(tcclGraphNode_t::MemOps);

    comm->gm_normal = new GraphManager(nodes);

    // graph manager for 8 card data (data size < 32K)
    nodes.clear();
    nodes.push_back(tcclGraphNode_t::MemOps);
    nodes.push_back(tcclGraphNode_t::Kernel);

    if (comm->nranks != dev_num) {
        for (int stride = 1; stride < card_num/2; stride *= 2) {
            nodes.insert(nodes.end(), {
                tcclGraphNode_t::IpcP2p,
                tcclGraphNode_t::IpcP2p,
                tcclGraphNode_t::MemOps });
        }
        nodes.push_back(tcclGraphNode_t::Kernel);
        nodes.push_back(tcclGraphNode_t::IpcP2p);
        nodes.push_back(tcclGraphNode_t::IpcP2p);
        nodes.push_back(tcclGraphNode_t::MemOps);
        nodes.push_back(tcclGraphNode_t::Kernel);
    }
    nodes.push_back(tcclGraphNode_t::MemOps);

    comm->gm_calc_last = new GraphManager(nodes);

    // graph manager for > 128KB situation: calc and translate
    nodes.clear();
    nodes.push_back(tcclGraphNode_t::MemOps);
    nodes.push_back(tcclGraphNode_t::Kernel);
    /*More than one phy card*/
    if (comm->nranks != dev_num) {
        for (int stride = 1; stride < card_num; stride *= 2) {
            nodes.insert(nodes.end(), {
                tcclGraphNode_t::IpcP2p,
                tcclGraphNode_t::IpcP2p,
                tcclGraphNode_t::MemOps});
            nodes.push_back(tcclGraphNode_t::Kernel);
        }
    }
    nodes.push_back(tcclGraphNode_t::MemOps);
    comm->gm_ct = new GraphManager(nodes);

    // graph manager for > 128KB situation: a mutation of butterfly algo
    nodes.clear();
    nodes.push_back(tcclGraphNode_t::MemOps);
    nodes.push_back(tcclGraphNode_t::ResideKernel);
    /*More than one phy card*/
    if (comm->nranks != dev_num) {
        for (int stride = 1; stride < card_num; stride *= 2) {
            nodes.insert(nodes.end(), {
                tcclGraphNode_t::MemOps,
                tcclGraphNode_t::IpcP2p,
                tcclGraphNode_t::IpcP2p});
        }
        for (int stride = card_num / 2; stride >= 1; stride /= 2) {
            nodes.insert(nodes.end(), {
                tcclGraphNode_t::MemOps,
                tcclGraphNode_t::IpcP2p,
                tcclGraphNode_t::IpcP2p});
        }
    }
    nodes.push_back(tcclGraphNode_t::MemOps);

    comm->gm_btmut = new GraphManager(nodes);

    return tcclSuccess;
}

tcclResult_t tcclCommInitRankSync(tcclComm_t* newcomm, int nranks, tcclUniqueId* commId, int myrank, int devid, tcclConfig_t* config = NULL) {
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point end_time;
    std::chrono::duration<double, std::milli> elapsed_time;

    tcclShmDirInit();
    PS();
    TCCLCHECK(tcclCommAlloc(newcomm, nranks, myrank));
    PE("tcclCommAlloc");
    //one rank card comm  and furtrue theeis more card in one rank;
    tcclComm_t comm;
    comm = *newcomm;
    comm->nranks = nranks;
    comm->rank = myrank;
    comm->devid = devid;
    comm->pid = getPid();
    comm->gm_calc_last = nullptr;
    comm->gm_normal = nullptr;
    comm->gm_ct = nullptr;
    comm->gm_btmut = nullptr;
    if (config) memcpy((void*)&comm->config, (void*)config, sizeof(tcclConfig_t));

    // if $ENABLE_P2PRECV == "true"
    comm->recv_enable = false;
    char *recv_enable = getenv("TCCL_ENABLE_P2PRECV");
    if (recv_enable != NULL && atoi(recv_enable) == 1) {
        comm->recv_enable = true;
    }

    if (comm->nranks == 1) {
        comm->isOneNode = true;
        return tcclSuccess;
    }
    getEnvFromFile();

    PS();
    tcclCrossBuffInit(comm);
    PE("tcclCrossBuffInit");

    memcpy(&comm->id, commId, sizeof(tcclUniqueId));
    struct sockaddr_in *addr = (struct sockaddr_in *)&comm->id;
    comm->port = (int)ntohs(addr->sin_port);

    PS();
    TCCLCHECK(tcclCommSocketInit(comm, commId));
    TCCLCHECK(tcclTraceLoginit(comm));
    PE("tcclCommSocketInit");

    PS();
    if (comm->node->rank_id == 0) {
        TCCLCHECK(tcclGraphManagerInit(comm));
    }
    PE("tcclGraphManagerInit");

    PS();
    TCCLCHECK(tcclShmAddrInit(comm));
    PE("tcclShmAddrInit");
    PS();
    TCCLCHECK(tcclDevListInit(comm));
    PE("tcclDevListInit");
    PS();
    TCCLCHECK(tcclDevSyncInit(comm));
    PE("tcclDevSyncInit");
    PS();
    TCCLCHECK(tcclPublicInit(comm));
    PE("tcclPublicInit");

    if (myrank == 0) TCCL_DEBUG_LOG("Init Successfully!!\n");
    return tcclSuccess;
}

tcclResult_t tcclGroupStart() {
    return tcclSuccess;
}

tcclResult_t tcclGroupEnd() {
    return tcclSuccess;
}

tcclResult_t tcclCommInitRank(tcclComm_t *comm, int nranks, tcclUniqueId commIdRef, int rank){
    tcclUniqueId *commId = &commIdRef;
    TCCLCHECK(PtrCheck(comm, "CommInitRank", "newcomm"));

    if (nranks < 1 || rank < 0 || rank >= nranks) {
        printf("Invalid rank requested : %d/%d\n", rank, nranks);
        return tcclInvalidArgument;
    }

    int devid;
    SDAACHECK(sdaaGetDevice(&devid));
    TCCLCHECK(tcclDevNumInit());
    return tcclCommInitRankSync(comm, nranks, commId, rank, devid);
}

tcclResult_t tcclGetUniqueId(tcclUniqueId *uniqueId) {
    TCCLCHECK(tcclBootstrapNetInit());
    TCCLCHECK(PtrCheck(uniqueId, "GetUniqueId", "uniqueId"));
    return tcclBootstrapGetUniqueId(uniqueId);
}

tcclResult_t tcclCommInitRankConfig(tcclComm_t* comm, int nranks, tcclUniqueId commId, int rank, tcclConfig_t* config) {
    tcclUniqueId *uniqueId = &commId;
    TCCLCHECK(PtrCheck(comm, "CommInitRank", "newcomm"));

    if (nranks < 1 || rank < 0 || rank >= nranks) {
        printf("Invalid rank requested : %d/%d\n", rank, nranks);
        return tcclInvalidArgument;
    }

    int devid;
    SDAACHECK(sdaaGetDevice(&devid));
    TCCLCHECK(tcclDevNumInit());
    return tcclCommInitRankSync(comm, nranks, uniqueId, rank, devid, config);
}

tcclResult_t tcclCommInitAll(tcclComm_t* comms, int ndev, const int* devlist) {
    int dev_cnt;
    int max_card_num;
    int *list = NULL;
    int* devs_flag = NULL;
    tcclResult_t *result = NULL;
    tcclResult_t ret = tcclSuccess;
    std::vector<std::thread> threads;
    SDAACHECK(sdaaGetDeviceCount(&dev_cnt));

    TCCLCHECK(PtrCheck(comms, "CommInitAll", "comms"));
    if (ndev < 0 || ndev > dev_cnt) {
        printf("Invalid ndev requested : %d\n", ndev);
        ret = tcclInvalidArgument;
        goto out;
    }

    // check devlist
    TCCLCHECK(tcclDevNumInit());
    max_card_num = dev_cnt / g_devnum;
    if (devlist) {
        int rank_num = 0;
        TCCLCHECKGOTO(ret = tcclCalloc(&devs_flag, dev_cnt));
        for (int i = 0; i < ndev; ++i) {
            // invalid device check
            if (devlist[i] < 0 || devlist[i] >= dev_cnt) {
                printf("Invalid device id requested : %d\n", devlist[i]);
                ret = tcclInvalidArgument;
                goto out;
            }

            // duplicate device check
            if (devs_flag[devlist[i]] != 0) {
                printf("duplicate device id requested : %d\n", devlist[i]);
                ret = tcclInvalidUsage;
                goto out;
            }
            devs_flag[devlist[i]] = 1;
        }

        // check same number of spa used in every aicard
        for (int card = 0; card < max_card_num; card++) {
            int tmp_rank_num = 0;
            for (int rank_id = 0; rank_id < g_devnum; rank_id++) {
                tmp_rank_num += devs_flag[card * g_devnum + rank_id];
            }
            if (tmp_rank_num != 0) {
                if (rank_num == 0) {
                    rank_num = tmp_rank_num;
                } else if (tmp_rank_num != rank_num) {
                    printf("Invalid devlist: please keep same number of dev used in each aicard\n");
                    ret = tcclInvalidArgument;
                    goto out;
                }
            }
        }
    } else {
        // construct devlist
        int rank_num;
        int card_num;
        for (rank_num = g_devnum; rank_num >= 1; rank_num--) {
            if (ndev % rank_num != 0) continue;
            card_num = ndev / rank_num;
            if (card_num <= max_card_num) break;
        }
        if (rank_num == 0) {
            printf("Invalid ndev requested : %d, can't init devlist\n", ndev);
            ret = tcclInvalidArgument;
            goto out;
        }

        TCCLCHECKGOTO(ret = tcclCalloc(&list, ndev));
        int idx = 0;
        for (int card = 0; card < card_num; card++) {
            for (int rank_id = 0; rank_id < rank_num; rank_id++) {
                list[idx] = card * rank_num + rank_id;
                idx++;
            }
        }
    }

    // get uniqueId
    tcclUniqueId uniqueId;
    TCCLCHECKGOTO(ret = tcclGetUniqueId(&uniqueId));
    tcclShmDirInit();

    // init tccl
    TCCLCHECKGOTO(ret = tcclCalloc(&result, ndev));
    for (int i = 0; i < ndev; i++) {
        std::thread t([=]() {
            int devid = devlist != NULL ? devlist[i] : list[i];
            SDAACHECK(sdaaSetDevice(devid));
            result[i] = tcclCommInitRankSync(comms+i, ndev, (tcclUniqueId *)&uniqueId, i, devid);
        });
        threads.push_back(std::move(t));
    }
    for (auto& t : threads) t.join();

    for (int i = 0; i < ndev; i++) {
        if (result[i] != tcclSuccess) {
            ret = result[i];
            break;
        }
    }

out:
    if (devs_flag) tcclFree(devs_flag);
    if (list) tcclFree(list);
    if (result) tcclFree(result);
    return ret;
}

tcclResult_t tcclCommFinalize(tcclComm_t comm) {
    sdaaError_t ret = sdaaStreamQuery(comm->user_stream);
    if (ret != sdaaSuccess) {
        SDAACHECK(sdaaStreamSynchronize(comm->user_stream));
    }
    
    /*if (ret != sdaaErrorContextIsDestroyed) {
        SDAACHECK(sdaaStreamSynchronize(comm->user_stream));
    }*/
    return tcclSuccess;
}

tcclResult_t tcclCommSplit(tcclComm_t comm, int color, int key, tcclComm_t *newcomm, tcclConfig_t* config) {
    int rank = comm->rank;
    int nranks = comm->nranks;
    int keyNum = 0;
    int rootRank;
    struct tcclSpiltInfo *info = NULL;
    tcclUniqueId *id = NULL;

    TCCLCHECKGOTO(tcclCalloc(&info, nranks));
    TCCLCHECKGOTO(tcclCalloc(&id, nranks));

    info[rank].color = color;
    info[rank].key = key;

    TCCLCHECKGOTO(tcclProxyAllGather(comm, (void *)info, sizeof(struct tcclSpiltInfo)));

    for (int i = 0; i < nranks; i++) {
        if (info[i].color == color) {
            keyNum++;
            if (info[i].key == 0) {
                rootRank = i;
            }
        }
    }

    if (key == 0 || color != TCCL_SPLIT_NOCOLOR) {
        TCCLCHECKGOTO(tcclGetUniqueId(&id[rank]));
    }

    TCCLCHECKGOTO(tcclProxyAllGather(comm, (void *)id, sizeof(tcclUniqueId)));

    if (color != TCCL_SPLIT_NOCOLOR) {
        tcclCommInitRankConfig(newcomm, keyNum, id[rootRank], key, config);
    }
    tcclFree(info);
    tcclFree(id);
    return tcclSuccess;
out:
    if (info) tcclFree(info);
    if (id) tcclFree(id);
    return tcclInternalError;
}

const char*  tcclGetLastError(tcclComm_t comm) {
    return tcclLastError;
}

tcclResult_t tcclCommGetAsyncError(tcclComm_t comm, tcclResult_t *asyncError) {
    sdaaError_t ret = sdaaStreamQuery(comm->user_stream);
    switch (ret) {
        case sdaaSuccess:
            *asyncError = tcclSuccess; break;
        case sdaaErrorNotReady:
            *asyncError = tcclInProgress; break;
        //case sdaaErrorContextIsDestroyed:
          //  *asyncError = tcclInvalidUsage; break;
        default:
            *asyncError = tcclNumResults; break;
    }
    return tcclSuccess;
}

const char *tcclGetErrorString(tcclResult_t code)
{
    switch (code) {
        case tcclSuccess                : return "no error";
        case tcclUnhandledSdaaError     : return "unhandled sdaa error";
        case tcclSystemError            : return "unhandled system error";
        case tcclInternalError          : return "internal error";
        case tcclInvalidArgument        : return "invalid argument";
        case tcclInvalidUsage           : return "invalid usage";
        case tcclInProgress             : return "TCCL operation in progress";
        default                         : return "unknown result code";
    }
}

tcclResult_t tcclCommCount(const tcclComm_t comm, int *count)
{
    TCCLCHECK(PtrCheck(comm, "CommCount", "comm"));
    TCCLCHECK(PtrCheck(count, "CommCount", "count"));
    *count = comm->nranks;
    return tcclSuccess;
}

tcclResult_t tcclCommDevice(const tcclComm_t comm, int *device)
{
    TCCLCHECK(PtrCheck(comm, "CommUserRank", "comm"));
    TCCLCHECK(PtrCheck(device, "CommUserRank", "device"));
    *device = comm->devid;
    return tcclSuccess;
}

tcclResult_t tcclCommUserRank(const tcclComm_t comm, int *rank)
{
    TCCLCHECK(PtrCheck(comm, "CommUserRank", "comm"));
    TCCLCHECK(PtrCheck(rank, "CommUserRank", "rank"));
    *rank = comm->rank;
    return tcclSuccess;
}

tcclResult_t tcclCommDestroy(tcclComm_t comm) {
    if (comm->nranks == 1) return tcclSuccess;
    TCCLCHECK(tcclCommFinalize(comm));

    tcclShmFree(comm->list, 4 * DEV_NUM * sizeof(void *));
    int card_id = comm->node->card_id;
    int rank_id = comm->node->rank_id;

    comm->list = NULL;

    tcclRankMemClose(comm);

    tcclPublicFini(comm);

    if (rank_id != 0) {
        if (!comm->sync.sameProc) SDAACHECK(sdaaIpcCloseMemHandle(comm->sync.nextcard));
        if (!comm->sync.sameProc) SDAACHECK(sdaaIpcCloseMemHandle(comm->sync.peercard));
        if (!comm->sync.sameProc) SDAACHECK(sdaaIpcCloseMemHandle(comm->sync.a2acard));
    }
    for (int i = 0; i < comm->node->card_num; i++) {
        if (i != card_id) {
            if (!comm->sync.sameProcList[i]) SDAACHECK(sdaaIpcCloseMemHandle(comm->sync.nextlist[i]));
            if (!comm->sync.sameProcList[i]) SDAACHECK(sdaaIpcCloseMemHandle(comm->sync.peerlist[i]));
            if (!comm->sync.sameProcList[i]) SDAACHECK(sdaaIpcCloseMemHandle(comm->sync.a2alist[i]));
        }
    }
    if (comm->node->rank_id == 0) {
        SDAACHECK(sdaaFree(comm->devList));
        SDAACHECK(sdaaFree(comm->sync.nextcard));
        SDAACHECK(sdaaFree(comm->sync.peercard));
        SDAACHECK(sdaaFree(comm->sync.syncflag));
        SDAACHECK(sdaaFree(comm->sync.cntflag));
        SDAACHECK(sdaaFree(comm->sync.splitFlag));
        SDAACHECK(sdaaFree(comm->sync.a2acard));

        if (comm->gm_normal!= nullptr) delete comm->gm_normal;
        if (comm->gm_calc_last!= nullptr) delete comm->gm_calc_last;
        if (comm->gm_ct!= nullptr) delete comm->gm_ct;
        if (comm->gm_btmut!= nullptr) delete comm->gm_btmut;
    }
    // free socket
    tcclSocket *sharpNext = comm->node->sharpNext;
    tcclSocket *sharpPrev = comm->node->sharpPrev;
    if (sharpNext != NULL) {
        close(sharpNext->fd);
        TCCLCHECK(tcclFree(sharpNext));
    }
    if (sharpPrev != NULL) {
        close(sharpPrev->fd);
        TCCLCHECK(tcclFree(sharpPrev));
    }
    comm->node->sharpNext = NULL;
    comm->node->sharpPrev = NULL;

    tcclDelShmFile(comm->port);
    TCCLCHECK(tcclCommSocketFini(comm));

    // Nic finalize
    tcclNicFini(comm);

    auto& fd = comm->nic_info[comm->rank].flag_devi;
    auto& tb = comm->nic_info[comm->rank].tmpBuff;

    SDAACHECK(sdaaMemDevicePutHostPointer(comm->addr.tmpHost));
    SDAACHECK(sdaaMemDevicePutHostPointer(comm->addr.flagHost));

    SDAACHECK(sdaaFree(fd));
    SDAACHECK(sdaaFree(tb));

    tcclFree(comm->nic_info);

    // free phyRankList
    SDAACHECK(sdaaFree(comm->allRing->devPhyRankList));
    if (comm->allRing->phyRankList) free(comm->allRing->phyRankList);

    free(comm);
    return tcclSuccess;
}

tcclResult_t tcclGetVersion(int *version) {
    if (version == NULL) {
        return tcclInvalidArgument;
    }
#ifdef _TCCL_VERNUM_
    *version = _TCCL_VERNUM_;
#else
    *version = 0;
#endif
    return tcclSuccess;
}

