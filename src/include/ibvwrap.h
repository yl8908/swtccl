#ifndef XCCL_IBVWRAP_H_
#define XCCL_IBVWRAP_H_
#include "core.h"
#include "ibvcore.h"

typedef enum ibv_return_enum
{
    IBV_SUCCESS = 0,                   //!< The operation was successful
} ibv_return_t;

#if !HAVE_DECL_IBV_QPF_GRH_REQUIRED
#  define IBV_QPF_GRH_REQUIRED                      0
#endif
// init ibv symbols as singleton
tcclResult_t wrap_ibv_symbols(void);
// warp ibv interface
tcclResult_t wrap_ibv_fork_init(void);
tcclResult_t wrap_ibv_get_device_list(struct ibv_device ***ret, int *num_devices);
tcclResult_t wrap_ibv_free_device_list(struct ibv_device **list);
const char *wrap_ibv_get_device_name(struct ibv_device *device);
tcclResult_t wrap_ibv_open_device(struct ibv_context **ret, struct ibv_device *device);
tcclResult_t wrap_ibv_close_device(struct ibv_context *context);
tcclResult_t wrap_ibv_get_async_event(struct ibv_context *context, struct ibv_async_event *event);
tcclResult_t wrap_ibv_ack_async_event(struct ibv_async_event *event);
tcclResult_t wrap_ibv_query_device(struct ibv_context *context, struct ibv_device_attr *device_attr);
tcclResult_t wrap_ibv_query_port(struct ibv_context *context, uint8_t port_num, struct ibv_port_attr *port_attr);
tcclResult_t wrap_ibv_query_gid(struct ibv_context *context, uint8_t port_num, int index, union ibv_gid *gid);
tcclResult_t wrap_ibv_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask, struct ibv_qp_init_attr *init_attr);
tcclResult_t wrap_ibv_alloc_pd(struct ibv_pd **ret, struct ibv_context *context);
tcclResult_t wrap_ibv_dealloc_pd(struct ibv_pd *pd);
tcclResult_t wrap_ibv_reg_mr(struct ibv_mr **ret, struct ibv_pd *pd, void *addr, size_t length, int access);
struct ibv_mr * wrap_direct_ibv_reg_mr(struct ibv_pd *pd, void *addr, size_t length, int access);
tcclResult_t wrap_ibv_reg_mr_iova2(struct ibv_mr **ret, struct ibv_pd *pd, void *addr, size_t length, uint64_t iova, int access);
/* DMA-BUF support */
tcclResult_t wrap_ibv_reg_dmabuf_mr(struct ibv_mr **ret, struct ibv_pd *pd, uint64_t offset, size_t length, uint64_t iova, int fd, int access);
struct ibv_mr * wrap_direct_ibv_reg_dmabuf_mr(struct ibv_pd *pd, uint64_t offset, size_t length, uint64_t iova, int fd, int access);
tcclResult_t wrap_ibv_dereg_mr(struct ibv_mr *mr);
tcclResult_t wrap_ibv_create_comp_channel(struct ibv_comp_channel **ret, struct ibv_context *context);
tcclResult_t wrap_ibv_destroy_comp_channel(struct ibv_comp_channel *channel);
tcclResult_t wrap_ibv_create_cq(struct ibv_cq **ret, struct ibv_context *context, int cqe, void *cq_context, struct ibv_comp_channel *channel, int comp_vector);
tcclResult_t wrap_ibv_destroy_cq(struct ibv_cq *cq);
static inline tcclResult_t wrap_ibv_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc, int* num_done) {
  int done = cq->context->ops.poll_cq(cq, num_entries, wc); /*returns the number of wcs or 0 on success, a negative number otherwise*/
  if (done < 0) {
    WARN("Call to ibv_poll_cq() returned %d", done);
    return tcclSystemError;
  }
  *num_done = done;
  return tcclSuccess;
}
tcclResult_t wrap_ibv_create_qp(struct ibv_qp **ret, struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr);
tcclResult_t wrap_ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask);
tcclResult_t wrap_ibv_destroy_qp(struct ibv_qp *qp);

static inline tcclResult_t wrap_ibv_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr){
  int ret = qp->context->ops.post_send(qp, wr, bad_wr); /*returns 0 on success, or the value of errno on failure (which indicates the failure reason)*/
  if (ret != IBV_SUCCESS) {
    WARN("ibv_post_send() failed with error %s, Bad WR %p, First WR %p", strerror(ret), wr, *bad_wr);
    return tcclSystemError;
  }
  return tcclSuccess;
}

static inline tcclResult_t wrap_ibv_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr, struct ibv_recv_wr **bad_wr){
  int ret = qp->context->ops.post_recv(qp, wr, bad_wr); /*returns 0 on success, or the value of errno on failure (which indicates the failure reason)*/
  if (ret != IBV_SUCCESS) {
    WARN("ibv_post_recv() failed with error %s", strerror(ret));
    return tcclSystemError;
  }
  return tcclSuccess;
}

tcclResult_t wrap_ibv_event_type_str(char **ret, enum ibv_event_type event);

#endif //End include guard
