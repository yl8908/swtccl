#ifndef TCCL_SHARPSYMBOLS_H
#define TCCL_SHARPSYMBOLS_H

#include "tccl.h"
#include "sharp_collcore.h"

struct tcclSharpCollSymbols{
    struct sharp_coll_config *sharp_coll_default_config;
    sharp_error_no (*sharp_coll_init)(struct sharp_coll_init_spec *sharp_coll_spec,
                                      struct sharp_coll_context **sharp_coll_context);
    sharp_error_no (*sharp_coll_comm_init)(struct sharp_coll_context *context, struct sharp_coll_comm_init_spec *spec,
                                        struct sharp_coll_comm **sharp_coll_comm);
    sharp_error_no (*sharp_coll_reg_mr)(struct sharp_coll_context *context, void *buf, size_t size, void **mr);
    const char *(*sharp_coll_strerror)(int error);
    sharp_error_no (*sharp_coll_do_bcast)(struct sharp_coll_comm *comm, struct sharp_coll_bcast_spec *spec);
    sharp_error_no (*sharp_coll_dereg_mr)(struct sharp_coll_context *context, void *mr);
    sharp_error_no (*sharp_coll_comm_destroy)(struct sharp_coll_comm *comm);
    sharp_error_no (*sharp_coll_finalize)(struct sharp_coll_context *context);
    sharp_error_no (*sharp_coll_do_allreduce)(struct sharp_coll_comm *comm, struct sharp_coll_reduce_spec *spec);
    sharp_error_no (*sharp_coll_caps_query)(struct sharp_coll_context *context, struct sharp_coll_caps *sharp_caps);
    sharp_error_no (*sharp_coll_reg_mr_v2)(struct sharp_coll_context *context, void *buf, size_t size,
                                           const struct sharp_coll_reg_params *params, void **mr);
    sharp_error_no (*sharp_coll_do_allreduce_nb)(struct sharp_coll_comm *comm, struct sharp_coll_reduce_spec *spec,
                                              void **handle);
    sharp_error_no (*sharp_coll_req_test)(void *handle);
    sharp_error_no (*sharp_coll_req_free)(void *handle);
};

tcclResult_t buildSharpCollSymbols(struct tcclSharpCollSymbols *sharpSymbols);

#endif  // TCCL_SHARPSYMBOLS_H
