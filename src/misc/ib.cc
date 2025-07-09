#include "ib.h"

#include "comm.h"

#define IB_PORT 1
#define SGID 0

int poll_completion(struct ibvResources *res, int poll_size) {
  struct ibv_wc wc[100];
  unsigned long start_time_msec;
  unsigned long cur_time_msec;
  struct timeval cur_time;
  int poll_result;
  int count = 0;

  gettimeofday(&cur_time, NULL);
  start_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
  do {
      ibv_cq *cq = res->cq;
      TCCLCHECK(wrap_ibv_poll_cq(cq, poll_size, wc, &poll_result));
      count += poll_result;
      gettimeofday(&cur_time, NULL);
      cur_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
  } while ((count < poll_size) && (poll_result >= 0) &&
           ((cur_time_msec - start_time_msec) < MAX_POLL_CQ_TIMEOUT));

  if (poll_result < 0) {
    tcclSetLastError("poll CQ failed\n");
  } else if (count < poll_size) {
    tcclSetLastError("completion wasn't found in the CQ after timeout, poll_size:%d\n", poll_size);
  } else {
  }


  return poll_size;
}

int post_write(struct ibvResources *res, int start, int length) {
  struct ibv_send_wr sr;
  struct ibv_sge sge;
  struct ibv_send_wr *bad_wr;
  int rc;

  /* prepare the scatter/gather entry */
  memset(&sge, 0, sizeof(sge));
  sge.addr = (uintptr_t)res->hostAddr + start;
  sge.length = length;
  sge.lkey = res->mr->lkey;

  /* prepare the send work request */
  memset(&sr, 0, sizeof(sr));
  sr.next = NULL;
  sr.wr_id = 0;
  sr.sg_list = &sge;
  sr.num_sge = 1;
  sr.opcode = IBV_WR_RDMA_WRITE;
  sr.send_flags = IBV_SEND_SIGNALED;
  sr.wr.rdma.remote_addr = res->remote_props.addr + start;
  sr.wr.rdma.rkey = res->remote_props.rkey;

  /* there is a Receive Request in the responder side, so we won't get any into
   * RNR flow */
  rc = wrap_ibv_post_send(res->qp, &sr, &bad_wr);
  if (rc) {
    printf("failed to post SR\n");
  }
  return rc;

  return 0;
}

void resources_init(struct ibvResources *res) {
  memset(res, 0, sizeof *res);
  return;
}


bool ib_device_check(struct ibv_device *dev) {
  bool ret = true;
  struct ibv_context *ctx = NULL;
  struct ibv_device_attr device_attr;
  struct ibv_port_attr port_attr;

  TCCLCHECK(wrap_ibv_open_device(&ctx, dev));
  if (ctx == NULL) return false;
  if (wrap_ibv_query_device(ctx, &device_attr) != 0) ret = false;
  if (ret && wrap_ibv_query_port(ctx, IB_PORT, &port_attr) != 0) ret = false;
  if (ret && (port_attr.state != IBV_PORT_ACTIVE ||
              port_attr.link_layer != IBV_LINK_LAYER_INFINIBAND))
    ret = false;

  TCCLCHECK(wrap_ibv_close_device(ctx));
  return ret;
}


/* Description: select ib dev by env TCCL_IB_DEV */
int tcclIBDevSelect(int devid, int num_devices) {
  const char *ib_dev = NULL;
  int ib_dev_idx[MAX_CARD_NUM] = {0};
  int ib_dev_current = 0;
  int len = 0;
  int env_size = 0;

  const char *env = "TCCL_IB_DEV";
  if (getenv(env)) {
    ib_dev = getenv(env);
  } else if (envMap.find(env) != envMap.end()) {
    ib_dev = envMap[env].data();
  }

  if (ib_dev != NULL) {
    env_size = strlen(ib_dev);
    for (int i = 0; i < MAX_CARD_NUM && len < env_size; i++) {
    while (len < env_size && (ib_dev[len] < '0' || ib_dev[len] > '9'))
        len++;
    int idx = strtoul(ib_dev + len, NULL, 0);
    ib_dev_idx[i] = idx < num_devices ? idx : 0;
    while (len < env_size && (ib_dev[len] >= '0' && ib_dev[len] <= '9'))
        len++;
    }
    int card_id = devid / DEV_NUM;
    ib_dev_current = ib_dev_idx[card_id];
  }
  return ib_dev_current;
}

int resources_create(struct ibvResources *res, char *hostAddr, char *deviAddr) {
  struct ibv_device **dev_list = NULL;
  struct ibv_qp_init_attr qp_init_attr;
  struct ibv_device_attr device_attr;
  int mr_flags = 0;
  int num_devices;
  int devid = -1;
  int ib_dev_current = 0;

  int ret = wrap_ibv_fork_init();
  if (ret != 0) {
    printf("ibv_fork_init failed !!!\n");
  }

  TCCLCHECK(wrap_ibv_get_device_list(&dev_list, &num_devices));
  if (!dev_list || !num_devices) {
    printf("failed to get IB devices list, num:%d\n", num_devices);
    goto err_out;
  }

  if (!res->ib_ctx) {
    SDAACHECK(sdaaGetDevice(&devid));
    ib_dev_current = tcclIBDevSelect(devid, num_devices);

    if (ib_dev_current >= 0 && ib_dev_current < num_devices) {
        wrap_ibv_open_device(&res->ib_ctx, dev_list[ib_dev_current]);
    }
    if (!res->ib_ctx) {
      printf("failed to open IB device\n");
      goto err_out;
    }
    TCCL_DEBUG_LOG("device %d bind to %s success.\n", devid,
                   dev_list[ib_dev_current]->name);
  }

  ret = wrap_ibv_query_device(res->ib_ctx, &device_attr);
  if (ret) {
      printf("ibv_query_device\n");
      goto err_out;
  }

  ret = wrap_ibv_query_port(res->ib_ctx, IB_PORT, &res->port_attr);
  if (ret) {
      printf("ibv_query_port on port %u failed\n", IB_PORT);
      goto err_out;
  }

  if (!res->pd) {
      wrap_ibv_alloc_pd(&res->pd, res->ib_ctx);
      if (!res->pd) {
          printf("ibv_alloc_pd failed\n");
          goto err_out;
      }
  }
  wrap_ibv_create_cq(&res->cq, res->ib_ctx, CQ_SIZE, NULL, NULL, 0);
  if (!res->cq) {
    printf("failed to create CQ with %u entries\n", CQ_SIZE);
    goto err_out;
  }

  res->hostAddr = hostAddr;
  res->deviAddr = deviAddr;

  if (res->size == 0) res->size = MAX_SIZE;

  if (!res->mr) {
    mr_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
               IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC |
               IBV_ACCESS_MW_BIND | IBV_ACCESS_RELAXED_ORDERING;
    wrap_ibv_reg_mr(&res->mr, res->pd, res->hostAddr, res->size, mr_flags);
    if (!res->mr) {
      printf("ibv_reg_mr failed with mr_flags=0x%x\n", mr_flags);
      goto err_out;
    }
  }

  memset(&qp_init_attr, 0, sizeof(qp_init_attr));
  qp_init_attr.qp_type = IBV_QPT_RC;
  qp_init_attr.sq_sig_all = 1;
  qp_init_attr.send_cq = res->cq;
  qp_init_attr.recv_cq = res->cq;
  qp_init_attr.cap.max_send_wr = CQ_SIZE;
  qp_init_attr.cap.max_recv_wr = CQ_SIZE;
  qp_init_attr.cap.max_send_sge = 1;
  qp_init_attr.cap.max_recv_sge = 1;
  wrap_ibv_create_qp(&res->qp, res->pd, &qp_init_attr);
  if (!res->qp) {
    printf("failed to create QP\n");
    goto err_out;
  }
  return 0;

err_out:
  if (res->qp) {
      wrap_ibv_destroy_qp(res->qp);
      res->qp = NULL;
  }
  if (res->mr) {
      wrap_ibv_dereg_mr(res->mr);
      res->mr = NULL;
  }
  if (res->cq) {
      wrap_ibv_destroy_cq(res->cq);
      res->cq = NULL;
  }
  if (res->pd) {
      wrap_ibv_dealloc_pd(res->pd);
      res->pd = NULL;
  }
  if (res->ib_ctx) {
      wrap_ibv_close_device(res->ib_ctx);
      res->ib_ctx = NULL;
  }
  if (dev_list) {
      wrap_ibv_free_device_list(dev_list);
      dev_list = NULL;
  }


  return 1;
}

int modify_qp_to_init(struct ibv_qp *qp) {
  int ret;
  struct ibv_qp_attr attr;

  memset(&attr, 0, sizeof(attr));
  attr.qp_state = IBV_QPS_INIT;
  attr.port_num = IB_PORT;
  attr.pkey_index = 0;
  attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                         IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC |
                         IBV_ACCESS_MW_BIND;
  ret = wrap_ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
  if (ret) {
    printf("failed to modify QP state to INIT\n");
  }
  return ret;

  return 0;
}

int modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid, uint8_t *gid) {
  int ret;
  int flags;
  struct ibv_qp_attr attr;

  memset(&attr, 0, sizeof(attr));
  attr.qp_state = IBV_QPS_RTR;
  attr.path_mtu = IBV_MTU_4096;
  attr.dest_qp_num = remote_qpn;
  attr.rq_psn = 0;
  attr.max_dest_rd_atomic = 1;
  attr.min_rnr_timer = 12;
  attr.ah_attr.is_global = 1;
  attr.ah_attr.grh.hop_limit = 1;
  memcpy((void *)&attr.ah_attr.grh.dgid, gid, 16);
  attr.ah_attr.grh.sgid_index = SGID;

  attr.ah_attr.dlid = dlid;
  attr.ah_attr.sl = 0;
  attr.ah_attr.src_path_bits = 0;
  attr.ah_attr.port_num = IB_PORT;

  flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
          IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
  ret = wrap_ibv_modify_qp(qp, &attr, flags);
  if (ret) {
    printf("failed to modify QP state to RTR\n");
  }
  return ret;

  return 0;
}

int modify_qp_to_rts(struct ibv_qp *qp) {
  int ret;
  int flags;
  struct ibv_qp_attr attr;

  memset(&attr, 0, sizeof(attr));
  attr.qp_state = IBV_QPS_RTS;
  attr.timeout = 14;
  attr.retry_cnt = 7;
  attr.rnr_retry = 7;
  attr.sq_psn = 0;
  attr.max_rd_atomic = 1;

  flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
          IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
  ret = wrap_ibv_modify_qp(qp, &attr, flags);
  if (ret) {
    printf("failed to modify QP state to RTS\n");
  }
  return ret;

  return 0;
}

int get_con_data(struct ibvResources *res) {
  res->local_props.addr = htonll((uintptr_t)res->hostAddr);
  res->local_props.rkey = htonl(res->mr->rkey);
  res->local_props.qp_num = htonl(res->qp->qp_num);
  res->local_props.lid = htons(res->port_attr.lid);
  memset(res->local_props.gid, 0, 16);
if (wrap_ibv_query_gid(res->ib_ctx, IB_PORT, SGID, (union ibv_gid *)res->local_props.gid)) {
    printf("can't read sgid of index %d\n", 1);
    return 1;
  }

  return 0;
}

int connect_qp(struct ibvResources *res, struct cm_con_data_t tmp_con_data) {
  int ret = 0;
  struct cm_con_data_t remote_con_data;

  remote_con_data.addr = ntohll(tmp_con_data.addr);
  remote_con_data.rkey = ntohl(tmp_con_data.rkey);
  remote_con_data.qp_num = ntohl(tmp_con_data.qp_num);
  remote_con_data.lid = ntohs(tmp_con_data.lid);
  memcpy(remote_con_data.gid, tmp_con_data.gid, 16);

  /* save the remote side attributes, we will need it for the post SR */
  res->remote_props = remote_con_data;

  ret = modify_qp_to_init(res->qp);
  if (ret) {
    goto err_out;
  }

  ret = modify_qp_to_rtr(res->qp, remote_con_data.qp_num, remote_con_data.lid, remote_con_data.gid);
  if (ret) {
    goto err_out;
  }

  ret = modify_qp_to_rts(res->qp);
  if (ret) {
    goto err_out;
  }

err_out:
  return ret;


  return 0;
}

int resources_destroy(struct ibvResources *res) {
  int rc = 0;

  if (res->qp) {
    if (wrap_ibv_destroy_qp(res->qp)) {
      fprintf(stderr, "failed to destroy QP\n");
      rc = 1;
    }
  }

  if (res->mr) {
    if (wrap_ibv_dereg_mr(res->mr)) {
      fprintf(stderr, "failed to deregister MR\n");
      rc = 1;
    }
  }

  if (res->cq) {
    if (wrap_ibv_destroy_cq(res->cq)) {
      fprintf(stderr, "failed to destroy CQ\n");
      rc = 1;
    }
  }

  if (res->pd) {
    if (wrap_ibv_dealloc_pd(res->pd)) {
      fprintf(stderr, "failed to deallocate PD\n");
      rc = 1;
    }
  }

  if (res->ib_ctx) {
    if (wrap_ibv_close_device(res->ib_ctx)) {
      fprintf(stderr, "failed to close device context\n");
      rc = 1;
    }
  }

  return rc;


  return 0;
}
