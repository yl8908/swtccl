#include "ibvsymbols.h"
#include "ibvwrap.h"
#include <pthread.h>
#include <errno.h>

static pthread_once_t initOnceControl = PTHREAD_ONCE_INIT;
static tcclResult_t initResult;
struct tcclIbvSymbols ibvSymbols;

tcclResult_t wrap_ibv_symbols(void) {
    pthread_once(&initOnceControl, []() { initResult = buildIbvSymbols(&ibvSymbols); });
    return initResult;
}

#define CHECK_NOT_NULL(symbols, function) \
    if (symbols.function == NULL) {       \
        return tcclInternalError;         \
    }

#define IBV_ERRNO_CHECK(symbols, function, call_function, error_retval, name) \
    CHECK_NOT_NULL(symbols, function);                                        \
    int ret = symbols.call_function;                                          \
    if (ret == error_retval) {                                                \
        return tcclSystemError;                                               \
    }                                                                         \
    return tcclSuccess;

#define IBV_SUCCESSNO_CHECK(symbols, function, call_function, success_retval, name) \
    CHECK_NOT_NULL(symbols, function);                                              \
    int ret = symbols.call_function;                                                \
    if (ret != success_retval) {                                                    \
        return tcclSystemError;                                                     \
    }                                                                               \
    return tcclSuccess;

#define IBV_RETVAL_CHECK(symbols, function, call_function, retval, error_retval, name) \
    CHECK_NOT_NULL(symbols, function);                                                 \
    retval = symbols.call_function;                                                    \
    if (retval == error_retval) {                                                      \
        return tcclSystemError;                                                        \
    }                                                                                  \
    return tcclSuccess;

tcclResult_t wrap_ibv_fork_init(void) {
    IBV_ERRNO_CHECK(ibvSymbols, tccl_ibv_fork_init, tccl_ibv_fork_init(), -1, "ibv_fork_init");
}

tcclResult_t wrap_ibv_get_device_list(struct ibv_device ***ret, int *num_devices) {
    *ret = ibvSymbols.tccl_ibv_get_device_list(num_devices);
    if (*ret == NULL) {
        *num_devices = 0;
    }
    return tcclSuccess;
}

tcclResult_t wrap_ibv_free_device_list(struct ibv_device **list) {
    IBV_ERRNO_CHECK(ibvSymbols, tccl_ibv_free_device_list, tccl_ibv_free_device_list(list), -1, "ibv_free_device_list");
}

const char *wrap_ibv_get_device_name(struct ibv_device *device) {
    if (ibvSymbols.tccl_ibv_get_device_name == NULL) {
        exit(-1);
    }
    return ibvSymbols.tccl_ibv_get_device_name(device);
}

tcclResult_t wrap_ibv_open_device(struct ibv_context **ret, struct ibv_device *device) {
    IBV_RETVAL_CHECK(ibvSymbols, tccl_ibv_open_device, tccl_ibv_open_device(device), *ret, NULL, "ibv_open_device");
}

tcclResult_t wrap_ibv_close_device(struct ibv_context *context) {
    IBV_ERRNO_CHECK(ibvSymbols, tccl_ibv_close_device, tccl_ibv_close_device(context), -1, "ibv_close_device");
}

tcclResult_t wrap_ibv_get_async_event(struct ibv_context *context, struct ibv_async_event *event) {
    IBV_ERRNO_CHECK(ibvSymbols, tccl_ibv_get_async_event, tccl_ibv_get_async_event(context, event), -1,
                    "ibv_get_async_event");
}

tcclResult_t wrap_ibv_ack_async_event(struct ibv_async_event *event) {
    IBV_ERRNO_CHECK(ibvSymbols, tccl_ibv_ack_async_event, tccl_ibv_ack_async_event(event), -1, "ibv_ack_async_event");
}

tcclResult_t wrap_ibv_query_device(struct ibv_context *context, struct ibv_device_attr *device_attr) {
    IBV_SUCCESSNO_CHECK(ibvSymbols, tccl_ibv_query_device, tccl_ibv_query_device(context, device_attr), 0,
                        "ibv_query_device");
}

tcclResult_t wrap_ibv_query_port(struct ibv_context *context, uint8_t port_num, struct ibv_port_attr *port_attr) {
    IBV_SUCCESSNO_CHECK(ibvSymbols, tccl_ibv_query_port, tccl_ibv_query_port(context, port_num, port_attr), 0,
                        "ibv_query_port");
}

tcclResult_t wrap_ibv_query_gid(struct ibv_context *context, uint8_t port_num, int index, union ibv_gid *gid) {
    IBV_SUCCESSNO_CHECK(ibvSymbols, tccl_ibv_query_gid, tccl_ibv_query_gid(context, port_num, index, gid), 0,
                        "wrap_ibv_query_gid");
}

tcclResult_t wrap_ibv_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask,
                               struct ibv_qp_init_attr *init_attr) {
    IBV_SUCCESSNO_CHECK(ibvSymbols, tccl_ibv_query_qp, tccl_ibv_query_qp(qp, attr, attr_mask, init_attr), 0,
                        "ibv_query_qp");
}

tcclResult_t wrap_ibv_alloc_pd(struct ibv_pd **ret, struct ibv_context *context) {
    IBV_RETVAL_CHECK(ibvSymbols, tccl_ibv_alloc_pd, tccl_ibv_alloc_pd(context), *ret, NULL, "ibv_alloc_pd");
}

tcclResult_t wrap_ibv_dealloc_pd(struct ibv_pd *pd) {
    IBV_SUCCESSNO_CHECK(ibvSymbols, tccl_ibv_dealloc_pd, tccl_ibv_dealloc_pd(pd), 0, "ibv_dealloc_pd");
}

tcclResult_t wrap_ibv_reg_mr(struct ibv_mr **ret, struct ibv_pd *pd, void *addr, size_t length, int access) {
    IBV_RETVAL_CHECK(ibvSymbols, tccl_ibv_reg_mr, tccl_ibv_reg_mr(pd, addr, length, access), *ret, NULL, "ibv_reg_mr");
}

struct ibv_mr *wrap_direct_ibv_reg_mr(struct ibv_pd *pd, void *addr, size_t length, int access) {
    if (ibvSymbols.tccl_ibv_reg_mr == NULL) {
        return NULL;
    }
    return ibvSymbols.tccl_ibv_reg_mr(pd, addr, length, access);
}

tcclResult_t wrap_ibv_reg_mr_iova2(struct ibv_mr **ret, struct ibv_pd *pd, void *addr, size_t length, uint64_t iova,
                                   int access) {
    if (ibvSymbols.tccl_ibv_reg_mr_iova2 == NULL) {
        return tcclInternalError;
    }
    if (ret == NULL) {
        return tcclSuccess;
    }
    IBV_RETVAL_CHECK(ibvSymbols, tccl_ibv_reg_mr_iova2, tccl_ibv_reg_mr_iova2(pd, addr, length, iova, access), *ret,
                     NULL, "ibv_reg_mr_iova2");
}

/* DMA-BUF support */
tcclResult_t wrap_ibv_reg_dmabuf_mr(struct ibv_mr **ret, struct ibv_pd *pd, uint64_t offset, size_t length,
                                    uint64_t iova, int fd, int access) {
    IBV_RETVAL_CHECK(ibvSymbols, tccl_ibv_reg_dmabuf_mr, tccl_ibv_reg_dmabuf_mr(pd, offset, length, iova, fd, access),
                     *ret, NULL, "ibv_reg_dmabuf_mr");
}

struct ibv_mr *wrap_direct_ibv_reg_dmabuf_mr(struct ibv_pd *pd, uint64_t offset, size_t length, uint64_t iova, int fd,
                                             int access) {
    if (ibvSymbols.tccl_ibv_reg_dmabuf_mr == NULL) {
        errno = EOPNOTSUPP;
        return NULL;
    }
    return ibvSymbols.tccl_ibv_reg_dmabuf_mr(pd, offset, length, iova, fd, access);
}

tcclResult_t wrap_ibv_dereg_mr(struct ibv_mr *mr) {
    IBV_SUCCESSNO_CHECK(ibvSymbols, tccl_ibv_dereg_mr, tccl_ibv_dereg_mr(mr), 0, "ibv_dereg_mr");
}

tcclResult_t wrap_ibv_create_cq(struct ibv_cq **ret, struct ibv_context *context, int cqe, void *cq_context,
                                struct ibv_comp_channel *channel, int comp_vector) {
    IBV_RETVAL_CHECK(ibvSymbols, tccl_ibv_create_cq, tccl_ibv_create_cq(context, cqe, cq_context, channel, comp_vector),
                     *ret, NULL, "tccl_ibv_create_cq");
}

tcclResult_t wrap_ibv_destroy_cq(struct ibv_cq *cq) {
    IBV_SUCCESSNO_CHECK(ibvSymbols, tccl_ibv_destroy_cq, tccl_ibv_destroy_cq(cq), 0, "ibv_destroy_cq");
}

tcclResult_t wrap_ibv_create_qp(struct ibv_qp **ret, struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr) {
    IBV_RETVAL_CHECK(ibvSymbols, tccl_ibv_create_qp, tccl_ibv_create_qp(pd, qp_init_attr), *ret, NULL, "ibv_create_qp");
}

tcclResult_t wrap_ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask) {
    IBV_SUCCESSNO_CHECK(ibvSymbols, tccl_ibv_modify_qp, tccl_ibv_modify_qp(qp, attr, attr_mask), 0, "ibv_modify_qp");
}

tcclResult_t wrap_ibv_destroy_qp(struct ibv_qp *qp) {
    IBV_SUCCESSNO_CHECK(ibvSymbols, tccl_ibv_destroy_qp, tccl_ibv_destroy_qp(qp), 0, "ibv_destroy_qp");
}

tcclResult_t wrap_ibv_event_type_str(char **ret, enum ibv_event_type event) {
    *ret = (char *)ibvSymbols.tccl_ibv_event_type_str(event);
    return tcclSuccess;
}
