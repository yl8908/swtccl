#ifndef TCCL_COMM_H
#define TCCL_COMM_H

#include <stdio.h>

#include "graph.h"
#include "ib.h"
#include "nic.h"
#include "proc.h"
#include "socket.h"
#include "tccl.h"
#include "utils.h"

#define WARN(...)
#define INFO(...)
#define TRACE(...)

#define THR_NUM 32
#define NOTIFY_NUM (1000)

#define MAX_SIZE (64 * 1024 * 1024)
#define ALIGNED_BYTE 4
#define SYNC_NUM 2

#define SMALL_SIZE (1024 * 1024)
#define SMALL_BUFF_SIZE (1024 * 1024 * 16)

struct tcclIP {
  uint8_t a : 8;
  uint8_t b : 8;
  uint8_t c : 8;
  uint8_t d : 8;
};

struct tcclMemHandle {
  uint64_t pid;
  void *ptr;
  sdaaIpcMemHandle_t handle;
};

struct tcclNicInfo {
  int guid;
  int phyCardId;
  int nodeId;
  int cardId;
  int rankId;
  uint64_t pid;

  int nextRank;
  int phyCardRank;

  char *inPcie;
  char *outPcie;
  char *inBuff;
  char *outBuff;
  sdaaIpcMemHandle_t inHandle;
  sdaaIpcMemHandle_t outHandle;

  char **tmpBuff;
  char **tmpPcie;
  sdaaIpcMemHandle_t tmpHandle;
  char **flag_devi;
  char **flag_pcie;
  sdaaIpcMemHandle_t flagHandle;
};

struct tcclDevid {
  int rank;
  int devid;
  int ipaddr;

  int phyCardId;
};

struct tcclCardRankList {
  int rankList[DEV_NUM_MAX];
};

struct tcclProxy {
  union tcclSocketAddress *proxyAddrList;
  struct tcclSocket *proxyListen;
  struct tcclSocket *allNext;
  struct tcclSocket *allPrev;
  struct tcclSocket *nodeNext;
  struct tcclSocket *nodePrev;
  struct tcclDevid *dev;
  struct tcclCardRankList *cardrank;
  int nodenext;
  int nodeprev;
  int sharpnext;
  int sharpprev;
};

typedef struct {
  char *inBuff;       /* device from root proc */
  char *inBuff_host;  /* the host address maped to inBuff */
  char *outBuff;      /* device from root proc */
  char *outBuff_host; /* the host address maped to inBuff */
  char *tmpBuff;      /* device from local proc */
  char *tmpHost;
  char *flagBuff;
  char *flagHost;

  char *inPcie;
  char *outPcie;
  char *tmpPcie;
  char *flagPcie;

  sdaaIpcMemHandle_t inHandle;
  sdaaIpcMemHandle_t outHandle;
} BUFF;

struct tcclDevSync {
  uint64_t syncnt;
  uint64_t *syncflag;

  uint64_t splitCnt;
  uint64_t *splitFlag;

  uint64_t *cntflag;

  uint64_t nextcnt;
  uint64_t *nextcard;
  uint64_t *nextlist[MAX_CARD_NUM];

  uint64_t prevcnt;

  uint64_t myBcastCnt;   /* count for each cross buffer in broadcast */
  uint64_t peerBcastCnt; /* count for each cross buffer in broadcast */
  uint64_t all2allcnt;

  uint64_t peercnt;
  uint64_t *peercard;
  uint64_t *peerlist[MAX_CARD_NUM];

  uint64_t a2acnt;
  uint64_t *a2acard;
  uint64_t *a2alist[MAX_CARD_NUM];

  // send/recv
  uint64_t *sendcntall;
  uint64_t *recvcntall;

  /* To judge if in the same process */
  bool sameProc;
  bool sameProcList[MAX_CARD_NUM];
};

typedef enum {
  TCCL_DMA = 0,
  TCCL_P2P = 1,
  TCCL_IB = 2,
  TCCL_NIC = 3,
  TCCL_BUTT
} tcclConnectType;

struct tcclAllRingInfo {
  int phyNextRank;
  int phyPrevRank;
  int phyCardRank;
  int *phyRankList;    // host
  int *devPhyRankList; // device

  int lgcNextRank;
  int lgcPrevRank;
  int lgcCardRank;

  int allCardNum;
};

struct tcclNetRingInfo {
  int nextCardRank;
  int prevCardRank;
  int nextNodeRank;
  int prevNodeRank;

  int myCardRank;
  int allCardNum;
};

struct tcclButterflyInfo {
  int roundNum;
  int *peerList;
};

struct tcclTreeInfo {
  int rootRank;
  int parentRank;
  int leftChild;
  int rightChild;
};

struct tcclIbPair {
  ibvResources send;
  ibvResources recv;
  struct cm_con_data_t remoteSendQps;
  struct cm_con_data_t remoteRecvQps;
  bool initialized = false;
};

struct tcclIbConn {
  struct tcclIbPair **sendpair;
  struct tcclIbPair **recvpair;
  struct tcclIbPair **flagpair;
  pthread_mutex_t *ibHostLock;
};

struct ib_table_element_t{
  struct tcclIbPair sendpair;
  struct tcclIbPair recvpair;
  struct tcclIbPair flagpair;
};

typedef ib_table_element_t IB_TABLE_ROW[32];
typedef int IP_ADDR;

struct tcclComm {
  int rank;
  int nranks;
  int devid;
  int dev_num;
  int optcnt;
  int port;
  bool isOneNode;
  size_t step_no;
  size_t graph_step_no;
  uint64_t pid; /* process id */
  tcclConfig_t config;
  sdaaStream_t user_stream;

  FILE *fp;
  void **list; /* shm */
  void *tmpList[DEV_NUM_MAX * 2];
  void **devList; /* cross shared in card */
  void **tmp;     /* cross in one rank */

  tcclUniqueId id;

  struct tcclSharpMap *sharpMap;
  struct tcclSharpMap *localSharp;

  struct tcclCardAddrMap *cardMap;
  BUFF addr;

  struct tcclDevSync sync;
  struct tcclNode *node;
  struct tcclNetRingInfo *netRing;
  struct tcclAllRingInfo *allRing;
  struct tcclProxy *proxy;
  volatile uint32_t *abortFlag;

  tcclConnectType *connectType;
  struct tcclIbConn allRank;
  //! swib
  struct nic_res *res;
  struct tcclNicInfo *nic_info;
  struct nic_res **sc_pair;

  /*graph manager*/
  GraphManager *gm_normal; /*the first graph manager, trans all and calc once*/
  GraphManager *gm_calc_last; /*just calc the last iteration*/
  GraphManager *gm_ct;        /*calc every iteration*/
  GraphManager *gm_btmut; /*a mutation version of butterfly for lager datasize*/
  int notify_comm_id;

  /* bcast */
  bool reuseCross;

  /*p2p recv mod*/
  bool recv_enable;
};

struct collArgs {
  size_t count;
  size_t data_size;
  tcclRedOp_t op;
  tcclDataType_t datatype;
  tcclColl_t coll;
  CAL_TYPE caltype;
  int bcastroot;
  size_t s_offset;
  size_t r_offset;

  int sendnum;
  int recvnum;
  int rank_id;
  int dev_num;
  sdaaStream_t stream;
  void **sendList;
  void **recvList;
  char *sendusr;
  char *recvusr;

  int flagnum;
  uint64_t *cntflag;
  uint64_t peercnt;
  void *crossIn;
  void *crossOut;
  bool useCrossBuff;
  bool needCopy;
  int sendOffsetNum;
  int nranks;
  size_t sliceSize;
  int rank;
  size_t realSize;
  bool recvUseCrossOut;
  int root_rankid;
  int *devPhyRankList;
};

struct resideKernelArgs {
  struct collArgs* coll_args;
  int dev_num;
  int card_id;
  int card_num;
  char* smallBuff;
  uint64_t *syncflag_addr;
  uint64_t syncflag_value;
  uint64_t *my_card_flag;
  void** recvusrs;
};

struct tcclSpiltInfo {
    int color;
    int key;
};

tcclResult_t tccl_move_to_cross(void *send, void *recv, size_t size, sdaaStream_t stream, bool isOneCard = false,
                                bool readDeviceSend = false, size_t send_off = 0,
                                bool readDeviceRecv = false, size_t recv_off = 0);
tcclResult_t tccl_move_to_cross_pro(void *send, void *recv, size_t size, sdaaStream_t stream);

tcclResult_t tccl_move_to_list(void *send, void **recvList, int recvnum,
                               size_t offset, size_t size, sdaaStream_t stream,
                               bool isOneCard = false);
tcclResult_t tcclBarrierAll(tcclComm_t comm);
tcclResult_t tcclBarrierNode(tcclComm_t comm);
tcclResult_t tcclBarrierCard(tcclComm_t comm);
tcclResult_t tcclDevSyncInit(tcclComm_t comm);
tcclResult_t tcclDevSyncRoot(tcclComm_t comm, sdaaStream_t stream,
                             GraphManager *graph_manager);
tcclResult_t tcclDevSyncOther(tcclComm_t comm, sdaaStream_t stream,
                              GraphManager *graph_manager);
tcclResult_t tcclDevSyncCard(tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclDevSyncToNextCard(tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclDevSyncToPrevCard(tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclDevSyncToLgcPrevCard(tcclComm_t comm, sdaaStream_t stream);
tcclResult_t tcclSendRecvNotify(tcclComm_t comm, int peer, sdaaStream_t stream, int* onlyWrite = NULL);
tcclResult_t tcclSendRecvWait(tcclComm_t comm, int peer, sdaaStream_t stream);
int tcclsizeof_datatype(tcclDataType_t datatype);
tcclResult_t tcclIbInitUnconnectedPeer(tcclComm_t comm, const int peer);
tcclResult_t tcclIbPairInitPeer3(tcclComm_t comm, int peer);

//! swib
tcclResult_t tcclNicSyncToNextCard(tcclComm_t comm, sdaaStream_t stream);

#endif
