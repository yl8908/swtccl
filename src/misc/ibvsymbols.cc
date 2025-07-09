#include "ibvsymbols.h"
#include "dlfcn.h"
#include "comm.h"
#define IBVERBS_VERSION "IBVERBS_1.1"

tcclResult_t buildIbvSymbols(struct tcclIbvSymbols* ibvSymbols){
    const char* use_ib = getenv("TCCL_USE_IB");
    if (use_ib != NULL && strcmp(use_ib, "0") == 0) {
        return tcclSuccess;
    }
    static void* ibvhandle = NULL;
    void** cast;
    void* tmp;
    if (use_ib == NULL || strcmp(use_ib, "1") == 0) {
        ibvhandle = dlopen("libibverbs.so", RTLD_NOW);
        if (!ibvhandle) {
            ibvhandle = dlopen("libibverbs.so.1", RTLD_NOW);
            if (!ibvhandle) {
                INFO(NCCL_INIT, "Failed to open libibverbs.so[.1]");
                goto teardown;
            }
        }
    } else {
        ibvhandle = dlopen(use_ib, RTLD_NOW);
        if (!ibvhandle){
            goto teardown;
        }
    }

#define LOAD_SYM(handle, symbol, funcptr)                                                    \
    do {                                                                                     \
        cast = (void**)&funcptr;                                                             \
        tmp = dlvsym(handle, symbol, IBVERBS_VERSION);                                       \
        if (tmp == NULL) {                                                                   \
            WARN("dlvsym failed on %s - %s version %s", symbol, dlerror(), IBVERBS_VERSION); \
            goto teardown;                                                                    \
        }                                                                                    \
        *cast = tmp;                                                                         \
    } while (0)

// Attempt to load a specific symbol version - fail silently
#define LOAD_SYM_VERSION(handle, symbol, funcptr, version)                                   \
    do {                                                                                     \
        cast = (void**)&funcptr;                                                             \
        *cast = dlvsym(handle, symbol, version);                                             \
        if (tmp == NULL) {                                                                   \
            WARN("dlvsym failed on %s - %s version %s", symbol, dlerror(), IBVERBS_VERSION); \
            goto teardown;                                                                    \
        }                                                                                    \
    } while (0)

    LOAD_SYM(ibvhandle, "ibv_fork_init", ibvSymbols->tccl_ibv_fork_init);
    LOAD_SYM(ibvhandle, "ibv_get_device_list", ibvSymbols->tccl_ibv_get_device_list);
    LOAD_SYM(ibvhandle, "ibv_free_device_list", ibvSymbols->tccl_ibv_free_device_list);
    LOAD_SYM(ibvhandle, "ibv_get_device_name", ibvSymbols->tccl_ibv_get_device_name);
    LOAD_SYM(ibvhandle, "ibv_open_device", ibvSymbols->tccl_ibv_open_device);
    LOAD_SYM(ibvhandle, "ibv_close_device", ibvSymbols->tccl_ibv_close_device);
    LOAD_SYM(ibvhandle, "ibv_get_async_event", ibvSymbols->tccl_ibv_get_async_event);
    LOAD_SYM(ibvhandle, "ibv_ack_async_event", ibvSymbols->tccl_ibv_ack_async_event);
    LOAD_SYM(ibvhandle, "ibv_query_device", ibvSymbols->tccl_ibv_query_device);
    LOAD_SYM(ibvhandle, "ibv_query_port", ibvSymbols->tccl_ibv_query_port);
    LOAD_SYM(ibvhandle, "ibv_query_gid", ibvSymbols->tccl_ibv_query_gid);
    LOAD_SYM(ibvhandle, "ibv_query_qp", ibvSymbols->tccl_ibv_query_qp);
    LOAD_SYM(ibvhandle, "ibv_alloc_pd", ibvSymbols->tccl_ibv_alloc_pd);
    LOAD_SYM(ibvhandle, "ibv_dealloc_pd", ibvSymbols->tccl_ibv_dealloc_pd);
    LOAD_SYM(ibvhandle, "ibv_reg_mr", ibvSymbols->tccl_ibv_reg_mr);
    // Cherry-pick the ibv_reg_mr_iova2 API from IBVERBS 1.8
    LOAD_SYM_VERSION(ibvhandle, "ibv_reg_mr_iova2", ibvSymbols->tccl_ibv_reg_mr_iova2, "IBVERBS_1.8");
    // Cherry-pick the ibv_reg_dmabuf_mr API from IBVERBS 1.12
    LOAD_SYM_VERSION(ibvhandle, "ibv_reg_dmabuf_mr", ibvSymbols->tccl_ibv_reg_dmabuf_mr, "IBVERBS_1.12");
    LOAD_SYM(ibvhandle, "ibv_dereg_mr", ibvSymbols->tccl_ibv_dereg_mr);
    LOAD_SYM(ibvhandle, "ibv_create_cq", ibvSymbols->tccl_ibv_create_cq);
    LOAD_SYM(ibvhandle, "ibv_destroy_cq", ibvSymbols->tccl_ibv_destroy_cq);
    LOAD_SYM(ibvhandle, "ibv_create_qp", ibvSymbols->tccl_ibv_create_qp);
    LOAD_SYM(ibvhandle, "ibv_modify_qp", ibvSymbols->tccl_ibv_modify_qp);
    LOAD_SYM(ibvhandle, "ibv_destroy_qp", ibvSymbols->tccl_ibv_destroy_qp);
    LOAD_SYM(ibvhandle, "ibv_event_type_str", ibvSymbols->tccl_ibv_event_type_str);
    // LOAD_SYM(ibvhandle, "ibv_query_ece", ibvSymbols->tccl_ibv_query_ece);
    // LOAD_SYM(ibvhandle, "ibv_set_ece", ibvSymbols->tccl_ibv_set_ece);

    return tcclSuccess;
teardown:
    ibvSymbols->tccl_ibv_get_device_list = NULL;
    ibvSymbols->tccl_ibv_free_device_list = NULL;
    ibvSymbols->tccl_ibv_get_device_name = NULL;
    ibvSymbols->tccl_ibv_open_device = NULL;
    ibvSymbols->tccl_ibv_close_device = NULL;
    ibvSymbols->tccl_ibv_get_async_event = NULL;
    ibvSymbols->tccl_ibv_ack_async_event = NULL;
    ibvSymbols->tccl_ibv_query_device = NULL;
    ibvSymbols->tccl_ibv_query_port = NULL;
    ibvSymbols->tccl_ibv_query_gid = NULL;
    ibvSymbols->tccl_ibv_query_qp = NULL;
    ibvSymbols->tccl_ibv_alloc_pd = NULL;
    ibvSymbols->tccl_ibv_dealloc_pd = NULL;
    ibvSymbols->tccl_ibv_reg_mr = NULL;
    ibvSymbols->tccl_ibv_reg_mr_iova2 = NULL;
    ibvSymbols->tccl_ibv_reg_dmabuf_mr = NULL;
    ibvSymbols->tccl_ibv_dereg_mr = NULL;
    ibvSymbols->tccl_ibv_create_cq = NULL;
    ibvSymbols->tccl_ibv_destroy_cq = NULL;
    if (ibvhandle){
        dlclose(ibvhandle);
        ibvhandle = NULL;
    }
    return tcclSystemError;
}
