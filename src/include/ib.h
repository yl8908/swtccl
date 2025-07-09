#ifndef TCCL_IB_H_
#define TCCL_IB_H_

#if !(defined _SWIB_) && !(defined _LNS8_) && !(defined _UELC20_) && !(defined _AARCH64_) && !(defined _8A_)
#endif
#include "ibvwrap.h"
#include <byteswap.h>
#include <sys/time.h>

#include "debug.h"
#include "socket.h"
#include "tccl.h"

/* poll CQ timeout in millisec (1800 seconds) */
#define MAX_POLL_CQ_TIMEOUT 1800000
#define MSG_SIZE (4096 * 4096)
#define POST_MSG_SIZE (MSG_SIZE * 16)
#define CQ_SIZE 256

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t htonll(uint64_t x) { return bswap_64(x); }
static inline uint64_t ntohll(uint64_t x) { return bswap_64(x); }
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t htonll(uint64_t x) { return x; }
static inline uint64_t ntohll(uint64_t x) { return x; }
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

/* structure to exchange data which is needed to connect the QPs */
struct cm_con_data_t {
    uint64_t addr;   /* Buffer address */
    uint32_t rkey;   /* Remote key */
    uint32_t qp_num; /* QP number */
    uint16_t lid;    /* LID of the IB port */
    uint8_t gid[16]; /* gid */
} __attribute__((packed));

/* structure of system ibvResources */
struct ibvResources {
    struct ibv_device_attr device_attr; /* Device attributes */
    struct ibv_port_attr port_attr;     /* IB port attributes */
    struct cm_con_data_t remote_props;  /* values to connect to remote side */
    struct cm_con_data_t local_props;   /* values to connect on the local */
    struct ibv_context *ib_ctx;         /* device handle */
    struct ibv_pd *pd;                  /* PD handle */
    struct ibv_cq *cq;                  /* CQ handle */
    struct ibv_qp *qp;                  /* QP handle */
    struct ibv_mr *mr;                  /* MR handle for buf */

    char *hostAddr;
    char *deviAddr;
    int size;
};

int get_con_data(struct ibvResources *res);
int poll_completion(struct ibvResources *res, int poll_size);
void resources_init(struct ibvResources *res);
int resources_create(struct ibvResources *res, char *hostAddr, char *deviAddr);
int connect_qp(struct ibvResources *res, struct cm_con_data_t tmp_con_data);
int post_write(struct ibvResources *res, int start, int length);
int resources_destroy(struct ibvResources *res);
int tcclIBDevSelect(int devid, int num_devices);

#endif
