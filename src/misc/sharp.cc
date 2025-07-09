#if !(defined _SWIB_) && !(defined _LNS8_) && !(defined _UELC20_) && !(defined _AARCH64_) && !(defined _8A_)
#define _USE_SHARP_
#endif
#include "sharp_collwrap.h"
#define HERE printf("here %d\n", __LINE__)
#include <unistd.h>
#include "shm.h"
#include "comm.h"
#include "sharp.h"
#include "collectives.h"
#include "perf.h"

#define SHARP_HEAD_LEN_SMALL 12
#define SHARP_HEAD_LEN_LARGE 16

static bool g_sharpEnable = false;
static int g_threshold = 2;
static struct sharp_coll_context *g_sharpContext = NULL;
static struct sharp_coll_comm *g_sharpComm = NULL;
static struct tcclSharpCtl *g_ctl = NULL;
static struct tcclSharpPub *g_local = NULL;
static pthread_t g_pubtid = 0;
static bool g_runningflag = false;

static bool g_isrootnode = false;
static int g_comm_num = 0;
struct tcclSharpMap *g_sharpMap[100] = { 0 };
struct tcclInfoList *g_info = NULL;
void *g_info_mr;

#define SLEEP_FOR_WAIT

#define SHARP_WAIT_FREE do { \
    while (g_ctl->status != TCCL_SHARP_FREE) { \
        SLEEP_FOR_WAIT; \
    } \
} while (0)

#define SHARP_WAIT_FREE_RANK(rankMap) do { \
    while (rankMap->status != TCCL_SHARP_FREE) { \
        SLEEP_FOR_WAIT; \
    } \
} while (0)

#define SHARP_WAIT_WORK_RANK(rankMap) do { \
    while (rankMap->status != TCCL_SHARP_WORK) { \
        SLEEP_FOR_WAIT; \
    } \
} while (0)

int tcclSocketOobBarrier(void *ctx) {
    tcclNode *node = (tcclNode *)ctx;
    uint64_t tmp = node->node_id;
    for (int i = 1; i < node->node_num; ++i){
        tcclSocketSend(node->sharpNext, &tmp, sizeof(tmp));
        tcclSocketRecv(node->sharpPrev, &tmp, sizeof(tmp));
    }
    return 0;
}

int tcclSocketOobBcast(void *ctx, void *buf, int size, int root) {
    tcclNode *node = (tcclNode *)ctx;
    if (node->node_id == root) {
        tcclSocketSend(node->sharpNext, buf, size);
        tcclSocketRecv(node->sharpPrev, buf, size);
    } else {
        tcclSocketRecv(node->sharpPrev, buf, size);
        tcclSocketSend(node->sharpNext, buf, size);
    }
    return 0;
}

int tcclSocketOobGather(void *ctx, int root, void *sbuf, void *rbuf, int size) {
    tcclNode *node = (tcclNode *)ctx;
    void *tmp = malloc(size * node->node_num);
    if (tmp == NULL) {
        SDAACHECK(sdaaErrorMemoryAllocation);
    }
    if (node->node_id == root) {
        memcpy((void *)((uintptr_t)tmp + size * (node->node_id)), sbuf, size);
        tcclSocketSend(node->sharpNext, tmp, size * node->node_num);
        tcclSocketRecv(node->sharpPrev, rbuf, size * node->node_num);
    } else {
        tcclSocketRecv(node->sharpPrev, tmp, size * node->node_num);
        memcpy((void *)((uintptr_t)tmp + size * (node->node_id)), sbuf, size);
        tcclSocketSend(node->sharpNext, tmp, size * node->node_num);
    }
    return 0;
}

tcclResult_t tcclSharpAllReduce(size_t count, tcclDataType_t datatype, tcclRedOp_t op, int offset, int devid);

void tcclSharpMemInit(int devid) {
    int ret;
    struct tcclSharpPub *map = &g_ctl->map[devid];
    if (devid != g_ctl->root_devid) {
        SDAACHECK(sdaaIpcOpenMemHandle((void **)&map->deviSend, map->handleSend, 0));
    }
    SDAACHECK(sdaaMemDeviceGetHostPointer((void **)&map->hostSend,  map->deviSend));

    wrap_sharp_coll_reg_mr(g_sharpContext, map->hostSend, MAX_SIZE, &map->s_mem_mr);

    map->devid = devid;
    return;
}

void tcclSharpMapInit(void) {
    for (int i = 0; i < g_comm_num; i++) {
        if (g_sharpMap[i]->comm_port == g_ctl->commport) {
            return;
        }
    }

    char nameMap[TCCL_SHM_NAME_LEN];
    (void)snprintf(nameMap, sizeof(nameMap), "SharpMap-%ld-%d", gethostid(), g_ctl->root_devid);
    struct tcclSharpMap *sharpMap = (struct tcclSharpMap *)tcclShmAlloc(nameMap, g_ctl->commport, MAX_CARD_NUM * DEV_NUM * sizeof(struct tcclSharpMap));
    if (sharpMap == NULL) {
        printf("%s shm malloc failed\n", nameMap);
        return;
    }
    int comm_id = g_comm_num;
    g_comm_num++;
    g_sharpMap[comm_id] = sharpMap;
    for (int i = 0; i < g_ctl->rank_no; i++) {
        sharpMap[i].comm_id = comm_id;
        sharpMap[i].comm_port = g_ctl->commport;
        sharpMap[i].status = TCCL_SHARP_FREE;
        int devid = sharpMap[i].devid;

        if (g_ctl->map[devid].devid == -1 && (sharpMap[i].total_rank_num == g_ctl->rank_no)) {
            tcclSharpMemInit(devid);
        }
    }
    return;
}

inline void tcclSharpWork(void) {
    TEST_GET_TIME(t0)
    struct tcclSharpMap *sharpMap = NULL;
#ifdef __LIST_ORDER_
    for (int comm_id = 0; comm_id < g_comm_num; comm_id++) {
        int total_rank_num = g_sharpMap[comm_id]->total_rank_num;
        for (int rank_no = 0; rank_no < total_rank_num; rank_no++) {
            if (g_sharpMap[comm_id][rank_no].status == TCCL_SHARP_WORK) {
                sharpMap = &g_sharpMap[comm_id][rank_no];
                break;
            }
        }
        if (sharpMap != NULL) break;
    }
#else
    int total_rank_num = 0;
    if (g_comm_num > 0) total_rank_num = g_sharpMap[0]->total_rank_num;

    for (int rank_no = 0; rank_no < total_rank_num; rank_no++) {
        for (int comm_id = 0; comm_id < g_comm_num; comm_id++) {
            if (g_sharpMap[comm_id][rank_no].status == TCCL_SHARP_WORK) {
                sharpMap = &g_sharpMap[comm_id][rank_no];
                break;
            }
        }
        if (sharpMap != NULL) break;
    }
#endif
    if (sharpMap == NULL) return;
    TEST_GET_TIME(t1)

    g_ctl->status = TCCL_SHARP_WORK;

    if (g_isrootnode) {
        g_info->rank_no = sharpMap->rank_no;
        g_info->comm_port = sharpMap->comm_port;
    }
    struct sharp_coll_bcast_spec bcast_spec  = { 0 };
    bcast_spec.buf_desc.buffer.ptr = g_info;
    bcast_spec.buf_desc.buffer.length = sizeof(struct tcclInfoList);
    bcast_spec.buf_desc.buffer.mem_handle = g_info_mr;
    bcast_spec.buf_desc.type = SHARP_DATA_BUFFER;
    bcast_spec.buf_desc.mem_type = SHARP_MEM_TYPE_HOST;
    bcast_spec.size = sizeof(struct tcclInfoList);
    bcast_spec.root = 0;
    wrap_sharp_coll_do_bcast(g_sharpComm, &bcast_spec);
    if (!g_isrootnode) {
        for (int i = 0; i < g_comm_num; i++) {
            if (g_info->comm_port == g_sharpMap[i]->comm_port) {
                sharpMap = &g_sharpMap[i][g_info->rank_no];
                break;
            }
        }
        SHARP_WAIT_WORK_RANK(sharpMap);
    }

    TEST_GET_TIME(t2)
    int count = sharpMap->count;
    tcclSharpAllReduce(sharpMap->count, sharpMap->datatype, sharpMap->op, sharpMap->offset, sharpMap->devid);
    TEST_GET_TIME(t3)

    sharpMap->status = TCCL_SHARP_FREE;
    g_ctl->status = TCCL_SHARP_FREE;
    TEST_CAL_TIME(find, t0, t1)
    TEST_CAL_TIME(bcast, t1, t2)
    TEST_CAL_TIME(sharp, t2, t3)
    // TEST_PRINTF("g_isrootnode:%d, find:%f, bcast:%f, sharp:%f(%f), len:%d, t1:%d.%d, t3:%d.%d\n",
    //     g_isrootnode, find, bcast, sharp, count*4/sharp/1024/1024, count*4, t1.tv_sec, t1.tv_usec, t3.tv_sec, t3.tv_usec);
    return;
}

void *tcclSharpPublicMain(void *args) {
    SDAACHECK(sdaaSetDevice(g_ctl->root_devid));
    while (g_runningflag) {
        if (g_ctl->status == TCCL_SHARP_FREE) {
            tcclSharpWork();
            SLEEP_FOR_WAIT;
        } else if (g_ctl->status == TCCL_SHARP_INIT) {
            tcclSharpMapInit();
            g_ctl->status = TCCL_SHARP_FREE;
        } else {
            SLEEP_FOR_WAIT;
        }
    }
    return NULL;
}

tcclResult_t tcclSharpPre(struct tcclNode *node) {
    char *sharp = getenv("TCCL_SHARP_ENABLE");
    TCCLCHECK(wrap_ibv_symbols());
    if ((sharp == NULL) || (strlen(sharp) > 50)) return tcclSuccess;

    if (atoi(sharp) != 0) {
        g_sharpEnable = true;
    } else {
        g_sharpEnable = false;
    }

    node->sharpEnable = g_sharpEnable;
    if (node->sharpEnable) {
        TCCLCHECK(wrap_sharp_symbols());
    }
    char *threshold = getenv("TCCL_SHARP_NODE_NUM");
    if ((threshold == NULL) || (strlen(threshold) > 50)) return tcclSuccess;

    g_threshold = atoi(threshold);

    return tcclSuccess;
}


tcclResult_t tcclSharpIbvNameGet(struct tcclNode *node) {
    int ibv_dev_num = 0;
    struct ibv_device **dev_list = NULL;

    TCCLCHECK(wrap_ibv_get_device_list(&dev_list, &ibv_dev_num));
    if (!dev_list || !ibv_dev_num) {
        printf("failed to get IB devices list, num:%d\n", ibv_dev_num);
        return tcclSystemError;
    }

    char *env = getenv("TCCL_SHARP_IB_DEV");
    if (env) {
        int sharp_ib_dev = strtoul(env, NULL, 0);
        if (sharp_ib_dev < 0 || sharp_ib_dev >= ibv_dev_num) sharp_ib_dev = 0;
        (void)snprintf(node->ibv_name, sizeof(node->ibv_name), "mlx5_%d:1", sharp_ib_dev);
        return tcclSuccess;
    }

    int i = 0;
    int ibvList[ibv_dev_num] = { 0 };
    char *list = getenv("TCCL_SHARP_IB_LIST");
    if (list != NULL) {
        int len = 0;
        int total_len = strlen(list);
        while (len < total_len) {
            ibvList[i] = strtoul(list+len, NULL, 0);
            if (ibvList[i] >= 10) {
                len+=3;
            } else {
                len+=2;
            }
            i++;
        }
    }

    int dev_cnt;
    if (i == 0) i = 1;
    SDAACHECK(sdaaGetDeviceCount(&dev_cnt));
    (void)snprintf(node->ibv_name, sizeof(node->ibv_name), "mlx5_%d:1", ibvList[node->devid/(dev_cnt/i)]);

    return tcclSuccess;
}

tcclResult_t tcclSharpCreate(struct tcclNode *node) {
    uint64_t job_id = (uint64_t)node->port << 32 + node->devid;
    int node_id = node->node_id;
    int node_num = node->node_num;
    int ret;

    setenv("SHARP_COLL_ENABLE_SAT", "1", 0);
    setenv("SHARP_COLL_NUM_COLL_GROUP_RESOURCE_ALLOC_THRESHOLD", "1", 0);
    setenv("SHARP_COLL_SAT_LOCK_BATCH_SIZE", "1", 0);
    setenv("SHARP_COLL_LOCK_ON_COMM_INIT", "1", 0);

    struct sharp_coll_context *sharpContext = NULL;
    struct sharp_coll_init_spec init_spec = { 0 };
    init_spec.progress_func  = NULL;
    init_spec.job_id = job_id;
    init_spec.world_rank = node_id;
    init_spec.world_size = node_num;
    init_spec.world_local_rank = node_id;
    init_spec.enable_thread_support = 0;
    init_spec.group_channel_idx = 0;

    init_spec.oob_colls.barrier = tcclSocketOobBarrier;
    init_spec.oob_colls.bcast = tcclSocketOobBcast;
    init_spec.oob_colls.gather = tcclSocketOobGather;
    init_spec.oob_ctx = node;
    init_spec.config = *wrap_default_sharp_coll_config();
    init_spec.config.user_progress_num_polls = 0;
    init_spec.config.ib_dev_list = node->ibv_name;

    TCCLCHECK(wrap_sharp_coll_init(&init_spec, &sharpContext));

    struct sharp_coll_comm *sharpComm = NULL;
    struct sharp_coll_comm_init_spec comm_spec = { 0 };
    comm_spec.rank = node_id;
    comm_spec.size = node_num;
    comm_spec.oob_ctx = node;
    comm_spec.group_world_ranks = NULL;

    TCCLCHECK(wrap_sharp_coll_comm_init(sharpContext, &comm_spec, &sharpComm));

    g_sharpContext = sharpContext;
    g_sharpComm = sharpComm;
    printf("tcclSharpCreate success!!!\n");

    return tcclSuccess;
}

tcclResult_t tcclSharpPublicInit(struct tcclNode *node) {
    int devid = node->devid;

    setenv("SHARP_COLL_ENABLE_PCI_RELAXED_ORDERING", "1", 0);

    char name[TCCL_SHM_NAME_LEN];
    (void)snprintf(name, sizeof(name), "ShmSharpPublic-%ld-%s", gethostid(), node->ibv_name);

    int initflag;
    if (node->node_id == 0) {
        initflag = 1;
        if (tcclIsShmExist(name, 0, sizeof(struct tcclSharpCtl))) {
            initflag = 0;
        }
        TCCLCHECK(tcclSocketSend(node->sharpNext, &initflag, sizeof(int)));
        TCCLCHECK(tcclSocketRecv(node->sharpPrev, &initflag, sizeof(int)));
    } else {
        TCCLCHECK(tcclSocketRecv(node->sharpPrev, &initflag, sizeof(int)));
        TCCLCHECK(tcclSocketSend(node->sharpNext, &initflag, sizeof(int)));
    }

    g_ctl = (struct tcclSharpCtl *)tcclShmAlloc(name, 0, sizeof(struct tcclSharpCtl));
    if (g_ctl == NULL) {
        return tcclSystemError;
    }

    if (initflag == 1) {
        g_isrootnode = node->node_id == 0 ? true : false;
        memset(g_ctl, 0xFF, sizeof(struct tcclSharpCtl));
        g_ctl->status = TCCL_SHARP_COMM;
        g_ctl->isrootnode = g_isrootnode;
        g_ctl->root_devid = devid;
        g_runningflag = true;
        pthread_create(&g_pubtid, NULL, tcclSharpPublicMain, NULL);
    }

    g_local = &g_ctl->map[devid];
    g_local->initflag = initflag;
    printf("%s, devid:%d, initflag:%d\n", name, devid, initflag);

    if (g_local->initflag == 1) {
        if (g_sharpContext == NULL || g_sharpComm == NULL) TCCLCHECK(tcclSharpCreate(node));

        g_info = (struct tcclInfoList *)malloc(sizeof(struct tcclInfoList));
        if (g_info == NULL) {
            return tcclSystemError;
        }
        TCCLCHECK(wrap_sharp_coll_reg_mr(g_sharpContext, g_info, sizeof(struct tcclInfoList), &g_info_mr));

        node->sharpEnable = g_sharpEnable;
        g_ctl->status = TCCL_SHARP_FREE;
    } else {
    }

    return tcclSuccess;
}

tcclResult_t tcclSharpInit(struct tcclNode *node) {
    if (g_sharpEnable == false || node->node_num < g_threshold) return tcclSuccess;

    if (g_ctl == NULL || g_local == NULL) TCCLCHECK(tcclSharpPublicInit(node));


    return tcclSuccess;
}

tcclResult_t tcclSharpFini(struct tcclNode *node) {
    int ret;
    if (node->sharpEnable == false) return tcclSuccess;
    if (g_local->initflag == 0) {
        g_local = NULL;
        g_ctl = NULL;
        return tcclSuccess;
    }
    if (g_pubtid == 0) {
        g_local = NULL;
        g_ctl = NULL;
        return tcclSuccess;
    }
    g_runningflag = false;
    pthread_join(g_pubtid, NULL);
    g_pubtid = 0;
    int dev_cnt;
    SDAACHECK(sdaaGetDeviceCount(&dev_cnt));

    for (int devid = 0; devid < dev_cnt; devid++) {
        struct tcclSharpPub *map = &g_ctl->map[devid];
        if (map->devid == -1) continue;
        TCCLCHECK(wrap_sharp_coll_dereg_mr(g_sharpContext, map->s_mem_mr));

        SDAACHECK(sdaaMemDevicePutHostPointer(map->hostSend));
        if (map->devid != g_ctl->root_devid) {
            SDAACHECK(sdaaIpcCloseMemHandle(map->deviSend));
        }
    }
    tcclShmFree(g_ctl, sizeof(struct tcclSharpCtl));
    g_ctl = NULL;
    g_local = NULL;

    for (int i = 0; i < g_comm_num; i++) {
        tcclShmFree(g_sharpMap[i], MAX_CARD_NUM * DEV_NUM * sizeof(struct tcclSharpMap));
        g_sharpMap[i] = NULL;
    }
    g_comm_num = 0;

    TCCLCHECK(wrap_sharp_coll_comm_destroy(g_sharpComm));
    TCCLCHECK(wrap_sharp_coll_finalize(g_sharpContext));
    g_sharpComm = NULL;
    g_sharpContext = NULL;
    tcclDelShmFile(0);

    return tcclSuccess;
}

tcclResult_t tcclGetSharpAddr(tcclComm_t comm) {
    if (g_ctl == NULL || g_local == NULL) {
        char name[TCCL_SHM_NAME_LEN];
        (void)snprintf(name, sizeof(name), "SharpPublic-%ld-%s", gethostid(), comm->node->ibv_name);

        g_ctl = (struct tcclSharpCtl *)tcclShmAlloc(name, 0, sizeof(struct tcclSharpCtl));
        if (g_ctl == NULL) {
            printf("%s shm malloc failed\n", name);
            return tcclSystemError;
        }
        g_local = &g_ctl->map[comm->devid];
    }
    SHARP_WAIT_FREE;
    g_local->handleSend = comm->addr.outHandle;
    g_local->deviSend = comm->addr.outBuff;

    char nameMap[TCCL_SHM_NAME_LEN];
    (void)snprintf(nameMap, sizeof(nameMap), "SharpMap-%ld-%d", gethostid(), g_ctl->root_devid);
    struct tcclSharpMap *sharpMap = (struct tcclSharpMap *)tcclShmAlloc(nameMap, comm->port, MAX_CARD_NUM * DEV_NUM * sizeof(struct tcclSharpMap));
    if (sharpMap == NULL) {
        printf("%s shm malloc failed\n", nameMap);
        return tcclSystemError;
    }

    struct tcclNode *node = comm->node;
    int rank_no = node->rank_num * node->card_id + node->rank_id;
    struct tcclSharpMap *localSharpMap = &sharpMap[rank_no];

    localSharpMap->devid = comm->devid;
    localSharpMap->rank_id = node->rank_id;
    localSharpMap->card_id = node->card_id;
    localSharpMap->rank_no = rank_no;
    localSharpMap->total_rank_num = node->card_num * node->rank_num;

    comm->sharpMap = sharpMap;
    comm->localSharp = localSharpMap;
    tcclBarrierAll(comm);
    if (g_local->initflag == 1) {
        SHARP_WAIT_FREE;
        g_ctl->commport = comm->port;
        g_ctl->rank_no = node->card_num * node->rank_num;
        g_ctl->status = TCCL_SHARP_INIT;
        SHARP_WAIT_FREE;
        sharpMap[0].total_rank_num = node->card_num * node->rank_num;
    }

    return tcclSuccess;
}

tcclResult_t tcclSharpAllReduce(size_t count, tcclDataType_t datatype, tcclRedOp_t op, int offset, int devid) {
    int typelen = tcclsizeof_datatype(datatype);
    sharp_datatype dtype[tcclNumTypes] = { SHARP_DTYPE_INT8,
                                         SHARP_DTYPE_UINT8,
                                         SHARP_DTYPE_INT,
                                         SHARP_DTYPE_UNSIGNED,
                                         SHARP_DTYPE_FLOAT,
                                         SHARP_DTYPE_DOUBLE,
                                         SHARP_DTYPE_LONG,
                                         SHARP_DTYPE_UNSIGNED_LONG,
                                         SHARP_DTYPE_FLOAT_SHORT };

    sharp_reduce_op sharp_op[tcclNumOps] = { SHARP_OP_SUM,
                                             SHARP_OP_PROD,
                                             SHARP_OP_MAX,
                                             SHARP_OP_MIN };

    struct tcclSharpPub *local = &g_ctl->map[devid];

    size_t total_len = count * typelen;
    int sharp_head_len = (typelen < 8) ? SHARP_HEAD_LEN_SMALL : SHARP_HEAD_LEN_LARGE;
    sharp_head_len = (total_len > sharp_head_len) ? sharp_head_len : total_len;
    int sharp_head_count = sharp_head_len / typelen;

    struct sharp_coll_reduce_spec reduce_spec = { 0 };
    reduce_spec.sbuf_desc.buffer.mem_handle = local->s_mem_mr;
    reduce_spec.sbuf_desc.type = SHARP_DATA_BUFFER;
    reduce_spec.sbuf_desc.mem_type = SHARP_MEM_TYPE_HOST;
    reduce_spec.rbuf_desc.buffer.mem_handle = local->s_mem_mr;
    reduce_spec.rbuf_desc.type = SHARP_DATA_BUFFER;
    reduce_spec.rbuf_desc.mem_type = SHARP_MEM_TYPE_HOST;

    reduce_spec.dtype = dtype[datatype];
    reduce_spec.aggr_mode = SHARP_AGGREGATION_NONE;
    reduce_spec.op = sharp_op[op];

    // sharp head
    reduce_spec.sbuf_desc.buffer.ptr = local->hostSend + offset;
    reduce_spec.sbuf_desc.buffer.length = sharp_head_len;
    reduce_spec.rbuf_desc.buffer.ptr = local->hostSend + offset;
    reduce_spec.rbuf_desc.buffer.length = sharp_head_len;
    reduce_spec.length = sharp_head_count;

    int ret = wrap_sharp_coll_do_allreduce(g_sharpComm, &reduce_spec);
    if (ret != 0) {
        tcclSetLastError("devid %d: sharp_coll_do_allreduce:%d, %s\n", devid, ret, wrap_sharp_coll_strerror(ret));
        return tcclInvalidUsage;
    }

    // sharp remaining data
    if (total_len > sharp_head_len) {
        reduce_spec.sbuf_desc.buffer.ptr = local->hostSend + offset + sharp_head_len;
        reduce_spec.sbuf_desc.buffer.length = total_len - sharp_head_len;
        reduce_spec.rbuf_desc.buffer.ptr = local->hostSend + offset + sharp_head_len;
        reduce_spec.rbuf_desc.buffer.length = total_len - sharp_head_len;
        reduce_spec.length = count - sharp_head_count;

        ret = wrap_sharp_coll_do_allreduce(g_sharpComm, &reduce_spec);
        if (ret != 0) {
            tcclSetLastError("devid %d: sharp_coll_do_allreduce(data):%d, %s\n", devid, ret, wrap_sharp_coll_strerror(ret));
            return tcclInvalidUsage;
        }
    }
    return tcclSuccess;
}

void tcclAllreduceInterFunc(void *args) {
    struct tcclSharpArgs *data = (struct tcclSharpArgs *)args;

    tcclComm_t comm = data->comm;
    struct tcclNode *node = comm->node;

    comm->localSharp->count = data->count;
    comm->localSharp->datatype = data->datatype;
    comm->localSharp->op = data->op;
    comm->localSharp->offset = data->offset;

    SHARP_WAIT_FREE_RANK(comm->localSharp);
    comm->localSharp->status = TCCL_SHARP_WORK;
    SHARP_WAIT_FREE_RANK(comm->localSharp);

    free(data);
    return;
}
