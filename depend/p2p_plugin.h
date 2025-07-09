/*************************************************************************
 * Copyright (c) 2016-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef XCCL_P2P_PLUGIN_H_
#define XCCL_P2P_PLUGIN_H_

#include <stdint.h>
#include <unistd.h>
#define ENABLE_TIMER 0
#include "timer.h"
#include <assert.h>

#include "xccl.h"
#include "net.h"
#include "ibvwrap.h"
#include "param.h"
#include "socket.h"
#include "utils.h"

#define MAXNAMESIZE 64
#define XCCL_NET_IB_MAX_RECVS 8
// We need to support XCCL_NET_MAX_REQUESTS for each concurrent receive
#define MAX_REQUESTS (XCCL_NET_MAX_REQUESTS*XCCL_NET_IB_MAX_RECVS)
//static_assert(MAX_REQUESTS <= 256, "request id are encoded in wr_id and we need up to 8 requests ids per completion");
#define IB_DEVICE_SYSFS_FMT "/sys/class/infiniband/%s/device/%s"


typedef enum xccl_p2p_plugin {
  XCCL_P2P_IB,
  XCCL_P2P_UCX,
  XCCL_P2P_UCX_RMA,
  XCCL_P2P_LAST
} xccl_p2p_plugin_t;

struct xcclIbMr {
  uintptr_t addr;
  int pages;
  int refs;
  struct ibv_mr *mr;
};

struct xcclIbMrCache {
  struct xcclIbMr *slots;
  int capacity, population;
};

struct xcclIbRequest {
  struct xcclIbVerbs* verbs;
  int type;
  int events;
  union xcclSocketAddress *addr;
  int nreqs;
  union {
    struct {
      int size;
      void* data;
      uint32_t lkey;
      int offset;
    } send;
    struct {
      int sizes[XCCL_NET_IB_MAX_RECVS];
    } recv;
  };
};

struct xcclIbVerbs {
  int    dev;
  struct ibv_pd* pd; // duplicate of xcclIbDevs[dev].pd
  struct ibv_cq* cq;
  uint64_t pad[1];
  struct xcclIbRequest reqs[MAX_REQUESTS];
};

typedef struct xcclIbDev {
  pthread_mutex_t lock;
  int      device;
  uint64_t guid;
  uint8_t  port;
  uint8_t  link;
  uint8_t  isSharpDev;
  int      speed;
  struct   ibv_context* context;
  int      pdRefs;
  struct ibv_pd*  pd;
  struct   xcclIbVerbs verbs;
  char     devName[MAXNAMESIZE];
  char     *pciPath;
  int      realPort;
  int      maxQp;
  struct   xcclIbMrCache mrCache;
} __attribute__((aligned(64))) xccl_ib_dev_t;

#define MAX_IB_PORT 15
struct userIbDev {
  char devName[MAXNAMESIZE];
  uint16_t port_en;
};

#define MAX_IB_DEVS 16
extern struct xcclIbDev xcclIbDevs[MAX_IB_DEVS];
extern struct xcclIbDev userIbDevs[MAX_IB_DEVS];
/* Detect whether GDR can work on a given NIC with the current CUDA device
 * Returns :
 * xcclSuccess : GDR works
 * xcclSystemError : no module or module loaded but not supported by GPU */
xcclResult_t xccl_p2p_gdr_support(int dev);

xcclResult_t xccl_p2p_ib_pci_path(xccl_ib_dev_t *devs, int num_devs, char* dev_name, char** path, int* real_port);

xcclResult_t xccl_p2p_ib_get_properties(xccl_ib_dev_t *devs, int dev, xcclNetProperties_t* props);

xcclResult_t xccl_p2p_ib_init(int *num_devs, xccl_ib_dev_t *xcclIbDevs, char *xcclIbIfName, union xcclSocketAddress *xcclIbIfAddr, pthread_t *xcclIbAsyncThread, xcclDebugLogger_t logFunction);

/* Convert value returtned by ibv_query_port to actual link width */
int xccl_p2p_ib_width(int width);

/* Convert value returtned by ibv_query_port to actual link speed */
int xccl_p2p_ib_speed(int speed);

int64_t xcclParamSharpMaxComms();

int xcclIbRelaxedOrderingCapable(void);

xccl_p2p_plugin_t xccl_p2p_get_plugin_type();

#endif
