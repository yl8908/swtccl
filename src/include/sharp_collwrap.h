#ifndef TCCL_SHARP_COLL_CORE_H
#define TCCL_SHARP_COLL_CORE_H
#include "sharp_collcore.h"
#include "tccl.h"
tcclResult_t wrap_sharp_symbols(void);

tcclResult_t wrap_sharp_coll_comm_init(sharp_coll_context *context, sharp_coll_comm_init_spec *spec,
                                       sharp_coll_comm **sharp_coll_comm);
tcclResult_t wrap_sharp_coll_comm_destroy(sharp_coll_comm *comm);
tcclResult_t wrap_sharp_coll_init(sharp_coll_init_spec *sharp_coll_spec, sharp_coll_context **sharp_coll_context);
tcclResult_t wrap_sharp_coll_reg_mr(sharp_coll_context *context, void *buf, size_t size, void **mr);
const char *wrap_sharp_coll_strerror(int error);
tcclResult_t wrap_sharp_coll_do_bcast(sharp_coll_comm *comm, sharp_coll_bcast_spec *spec);
tcclResult_t wrap_sharp_coll_dereg_mr(sharp_coll_context *context, void *mr);
tcclResult_t wrap_sharp_coll_finalize(sharp_coll_context *context);
sharp_error_no  wrap_sharp_coll_do_allreduce(sharp_coll_comm *comm, sharp_coll_reduce_spec *spec);
sharp_coll_config *wrap_default_sharp_coll_config();

#endif // TCCL_SHARP_COLL_CORE_H
