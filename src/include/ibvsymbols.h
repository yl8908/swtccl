#ifndef TCCL_IBVSYMBOLS_H
#define TCCL_IBVSYMBOLS_H
#include "ibvcore.h"
#include "tccl.h"
struct tcclIbvSymbols{
    int (*tccl_ibv_fork_init)(void);
    struct ibv_device **(*tccl_ibv_get_device_list)(int *num_devices);
    int (*tccl_ibv_free_device_list)(struct ibv_device **list);
    const char *(*tccl_ibv_get_device_name)(struct ibv_device *device);
    struct ibv_context *(*tccl_ibv_open_device)(struct ibv_device *device);
    int (*tccl_ibv_close_device)(struct ibv_context *context);
    int (*tccl_ibv_get_async_event)(struct ibv_context *context, struct ibv_async_event *event);
    int (*tccl_ibv_ack_async_event)(struct ibv_async_event *event);
    int (*tccl_ibv_query_device)(struct ibv_context *context, struct ibv_device_attr *device_attr);
    int (*tccl_ibv_query_port)(struct ibv_context *context, uint8_t port_num, struct ibv_port_attr *port_attr);
    int (*tccl_ibv_query_gid)(struct ibv_context *context, uint8_t port_num, int index, union ibv_gid *gid);
    int (*tccl_ibv_query_qp)(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask, struct ibv_qp_init_attr *init_attr);
    struct ibv_pd *(*tccl_ibv_alloc_pd)(struct ibv_context *context);
    int (*tccl_ibv_dealloc_pd)(struct ibv_pd *pd);
    struct ibv_mr * (*tccl_ibv_reg_mr)(struct ibv_pd *pd, void *addr, size_t length, int access);
    struct ibv_mr * (*tccl_ibv_reg_mr_iova2)(struct ibv_pd *pd, void *addr, size_t length, uint64_t iova, int access);
    /* DMA-BUF support */
    struct ibv_mr * (*tccl_ibv_reg_dmabuf_mr)(struct ibv_pd *pd, uint64_t offset, size_t length, uint64_t iova, int fd, int access);
    int (*tccl_ibv_dereg_mr)(struct ibv_mr *mr);
    // int (*tccl_ibv_create_comp_channel)(struct ibv_comp_channel **ret, struct ibv_context *context);
    // int (*tccl_ibv_destroy_comp_channel)(struct ibv_comp_channel *channel);
    struct ibv_cq *(*tccl_ibv_create_cq)(struct ibv_context *context, int cqe, void *cq_context,
                                         struct ibv_comp_channel *channel, int comp_vector);
    int (*tccl_ibv_destroy_cq)(struct ibv_cq *cq);
    struct ibv_qp *(*tccl_ibv_create_qp)(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr);
    int (*tccl_ibv_modify_qp)(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask);
    int (*tccl_ibv_destroy_qp)(struct ibv_qp *qp);
    const char *(*tccl_ibv_event_type_str)(enum ibv_event_type event);
    int (*tccl_ibv_query_ece)(struct ibv_qp *qp, struct ibv_ece *ece);
    int (*tccl_ibv_set_ece)(struct ibv_qp *qp, struct ibv_ece *ece);
};

tcclResult_t buildIbvSymbols(struct tcclIbvSymbols *ibvSymbols);
#endif  // TCCL_IBVSYMBOLS_H
