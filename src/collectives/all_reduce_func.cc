#include "collectives.h"
#include "comm.h"
#include "common_kernel.h"
#include "debug.h"

// #define LDM_SIZE (8 * 1024)
// #define LDM_SIZE (32 * 1024)
// #define LDM_SIZE_SM 2*1024

#define __PERF__
#ifdef __PERF__
#define ps() record(begin);
#define pe() \
  record(end); \
    printf("%.2f us\n", (float)((end-begin)*4)/10000); \

#define record(count) ({__asm__ __volatile__("rcsr %0, 0x4\n" : "=&r"(count)::"memory"); })

double get_time(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000;
}

#endif


#define synchroniz_array() \
  ({                            \
    __asm__ __volatile__(       \
        "ldi  $1,  0xff\n"      \
        "synr $1       \n"      \
        "ldi  $1,  0xf\n"       \
        "sync $1       \n" ::   \
            : "memory", "$1");  \
    0;                          \
  })

/*Begin Graph Moudule*/
//extern __global__ void ready(void);

void GraphManager::addEmptyKernelNode(int index, const sdaaGraphNode_t *depends){
    printf("yangl debug :%s,%d\n",__FUNCTION__, __LINE__);
    /*struct sdaaKernelNodeParams param;
    param.func = reinterpret_cast<void *>(ready);
    param.kernelParams = nullptr;
    void *extra[] = {SDAA_LAUNCH_PARAM_BUFFER_POINTER, nullptr,
                     SDAA_LAUNCH_PARAM_BUFFER_SIZE, nullptr,
                     SDAA_LAUNCH_PARAM_END};
    param.extra = extra;
    size_t numDependencies = (index == 0) ? 0 : 1;
    SDAACHECK(sdaaGraphAddKernelNode(&nodes_[index], graph_, depends,
                                     numDependencies, &param));*/
}

void GraphManager::addEmptyResideKernelNode(int index, const sdaaGraphNode_t *depends){
    printf("yangl debug :%s,%d\n",__FUNCTION__, __LINE__);
/*    struct sdaaKernelNodeParams param;
    param.func = reinterpret_cast<void *>(ready);
    param.kernelParams = nullptr;
    void *extra[] = {SDAA_LAUNCH_PARAM_BUFFER_POINTER, nullptr,
                     SDAA_LAUNCH_PARAM_BUFFER_SIZE, nullptr,
                     SDAA_LAUNCH_PARAM_END};
    param.extra = extra;
    size_t numDependencies = (index == 0) ? 0 : 1;
    SDAACHECK(sdaaGraphAddResideKernelNode(&nodes_[index], graph_, depends,
                                     numDependencies, &param));*/
}

/*End Graph Moudule*/

/*Make Sure struct tcclData is 8 bytes aligned(Add to graph)*/
typedef struct {
    int op;
    int type;
    int coll;
    size_t size;
    size_t count;
    int sendnum;
    int recvnum;
    int rank_id;
    int dev_num;
    size_t s_offset;
    size_t r_offset;
    void *sendList[MAX_CARD_NUM];
    void *recvList[MAX_CARD_NUM];
    void **s;
    void **r;

    int flagnum;
    uint64_t *cntflag;
    uint64_t flag;
    bool useCrossBuff;
    int sOffsetNum;
    int nranks;
    size_t data_size;
    size_t slice_size;
    bool recvUseCrossOut;
    int rank;
    int root_rankid;
    bool needCopy;
    int *devPhyRankList;
} tcclData;

/* struct used for reside kernel */
typedef struct {
    size_t data_size;
    size_t count;
    int sendnum;
    int recvnum;
    void *sendList[MAX_CARD_NUM];
    void *recvList[MAX_CARD_NUM];
    void *recvusrs[DEV_NUM_MAX];
    void **s;
    void **r;

    int flagnum;
    uint64_t *cntflag;
    uint64_t peercnt;
    volatile uint64_t *my_card_flag; //comm->sync.peerlist[card_id]
    int card_id;
    int card_num;
    char* smallBuff;
    uint64_t *syncflag_addr;
    uint64_t syncflag_value;
    int dev_num;
    int macro_dev_num;
} tcclResideKernelData;

tcclResult_t tccl_move_to_cross(void *send, void *recv, size_t size, sdaaStream_t stream, bool isOneCard,
                                bool readDeviceSend, size_t send_off, bool readDeviceRecv, size_t recv_off) {
   // tccl_move_data<<<1, stream>>>((char *)send, (char *)recv, size, isOneCard, readDeviceSend, send_off, readDeviceRecv, recv_off);
    printf("yangl debug :%s,%d\n",__FUNCTION__, __LINE__);
    return tcclSuccess;
}

tcclResult_t tccl_move_to_cross_pro(void *send, void *recv, size_t size, sdaaStream_t stream) {
    //tccl_move_data_pro<<<1, stream>>>((char *)send, (char *)recv, size);
    printf("David debug :%s,%d\n",__FUNCTION__, __LINE__);
    return tcclSuccess;
}

typedef struct {
    char *sendBuff;
    char **recvList;
    int recvnum;
    size_t offset;
    size_t size;
    bool isOneCard;
} tcclMove;

tcclResult_t tccl_move_to_list(void* send, void** recvList, int recvnum, size_t offset, size_t size, sdaaStream_t stream, bool isOneCard) {
    printf("yangl debug :%s,%d\n",__FUNCTION__, __LINE__);
    return tcclSuccess;
}


tcclResult_t tcclDeviceAlltoall(struct collArgs* args) {
    //tccl_move_data_alltoall<<<1, args->stream>>>(args->sendusr, (char **)args->recvList, args->rank_id, args->dev_num, args->data_size, args->nranks / args->dev_num);
    printf("yangl debug :%s,%d\n",__FUNCTION__, __LINE__);
    return tcclSuccess;
}

#define ADD_KERNEL_TO_GRAPH(kernel, argaddr, argsize, graph)         \
  do {                                                               \
    void *extra[] = {SDAA_LAUNCH_PARAM_BUFFER_POINTER, argaddr,      \
                     SDAA_LAUNCH_PARAM_BUFFER_SIZE, (void *)argsize, \
                     SDAA_LAUNCH_PARAM_END};                         \
    graph->updateGraphKernelNode((void *)kernel, nullptr, extra);    \
  } while (0)


#define ADD_RESIDE_KERNEL_TO_GRAPH(kernel, argaddr, argsize, graph)         \
  do {                                                                      \
    void *extra[] = {SDAA_LAUNCH_PARAM_BUFFER_POINTER, argaddr,             \
                     SDAA_LAUNCH_PARAM_BUFFER_SIZE, (void *)argsize,        \
                     SDAA_LAUNCH_PARAM_END};                                \
    graph->updateGraphResideKernelNode((void *)kernel, nullptr, extra);     \
  } while (0)

template tcclResult_t tcclDeviceAllReduce<8 * 1024>(struct collArgs *, GraphManager *);
template tcclResult_t tcclDeviceAllReduce<16 * 1024>(struct collArgs *, GraphManager *);
template <size_t LDM_SIZE>
tcclResult_t tcclDeviceAllReduce(struct collArgs *args, GraphManager *graph_manager) {

    printf("yangl debug :%s,%d\n",__FUNCTION__, __LINE__);
    return tcclSuccess;
}

tcclResult_t tcclDeviceAllReduceResideKernel(struct resideKernelArgs* reside_args, GraphManager *graph_manager) {
    printf("yangl debug :%s,%d\n",__FUNCTION__, __LINE__);
    return tcclSuccess;
}

template tcclResult_t tcclDeviceAllReduceOneProc<8 * 1024>(struct collArgs* args);
template tcclResult_t tcclDeviceAllReduceOneProc<16 * 1024>(struct collArgs* args);
template<size_t LDM_SIZE>
tcclResult_t tcclDeviceAllReduceOneProc(struct collArgs* args) {
    size_t count = args->count;
    size_t size = args->data_size;

    if (args->sendnum == 1) {
        //tccl_move_to_cross(args->sendList[0], args->recvList[0], size, args->stream);
        return tcclSuccess;
    }

    size = (size / ALIGNED_BYTE + 1) * ALIGNED_BYTE;
    count = size / tcclsizeof_datatype(args->datatype);
    printf("yangl debug :%s,%d\n",__FUNCTION__, __LINE__);
    return tcclSuccess;
}

template tcclResult_t tcclDeviceReduce<8 * 1024>(struct collArgs* args);
template tcclResult_t tcclDeviceReduce<16 * 1024>(struct collArgs* args);
template<size_t LDM_SIZE>
tcclResult_t tcclDeviceReduce(struct collArgs* args) {
    printf("yangl debug :%s,%d\n",__FUNCTION__, __LINE__);
    return tcclSuccess;
}


tcclResult_t tcclDeviceReady(sdaaStream_t stream) {
 //   ready<<<1, stream>>>();
    SDAACHECK(sdaaStreamSynchronize(stream));
    printf("yangl debug :%s,%d\n",__FUNCTION__, __LINE__);
    return tcclSuccess;
}

#define PHYSICAL 2000000000UL  // 2G
//#define __count_tc(count) ({ __asm__ __volatile__("rcsr %0, 0x4\n" : "=&r"(count) : : "memory"); })
#define __count_tc(count) ({})

//__global__ void __msleep(unsigned long msec) {
void __msleep(unsigned long msec) {
    volatile unsigned long count1, count2;
    __count_tc(count1);
    do {
        __count_tc(count2);
    } while (count2 - count1 < PHYSICAL/1000 * msec);
}

tcclResult_t tcclSleepKernel(sdaaStream_t stream, unsigned long msec) {
//    __msleep<<<1, stream>>>(msec);
    printf("yangl debug :%s,%d\n",__FUNCTION__, __LINE__);
    return tcclSuccess;
}

