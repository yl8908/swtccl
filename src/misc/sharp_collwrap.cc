#include "sharp_collwrap.h"
#include <pthread.h>
#include <errno.h>
#include "sharp_collsymbols.h"

static pthread_once_t initOnceControl = PTHREAD_ONCE_INIT;
static tcclResult_t initResult;
struct tcclSharpCollSymbols sharpSymbols;

tcclResult_t wrap_sharp_symbols(void) {
    pthread_once(&initOnceControl, []() { initResult = buildSharpCollSymbols(&sharpSymbols); });
    return initResult;
}

#define CHECK_NOT_NULL(symbols, function) \
    if (symbols.function == NULL) {       \
        return tcclInternalError;         \
    }

#define SHARP_COLL_ERRNO_CHECK(symbols, function, call_function, error_retval, name) \
    CHECK_NOT_NULL(symbols, function);                                               \
    int ret = symbols.call_function;                                                 \
    if (ret == error_retval) {                                                       \
        printf("%s:%d %s\n", #function, ret, wrap_sharp_coll_strerror(ret));         \
        return tcclSystemError;                                                      \
    }                                                                                \
    return tcclSuccess;

#define SHARP_COLL_SUCCESSNO_CHECK(symbols, function, call_function, success_retval, name) \
    CHECK_NOT_NULL(symbols, function);                                                     \
    int ret = symbols.call_function;                                                       \
    if (ret != success_retval) {                                                           \
        printf("%s:%d %s\n", #function, ret, wrap_sharp_coll_strerror(ret));               \
        return tcclSystemError;                                                            \
    }                                                                                      \
    return tcclSuccess;

#define SHARP_COLL_RETVAL_CHECK(symbols, function, call_function, retval, error_retval, name) \
    CHECK_NOT_NULL(symbols, function);                                                        \
    retval = symbols.call_function;                                                           \
    if (retval == error_retval) {                                                             \
        printf("%s:%d %s\n", #function, ret, wrap_sharp_coll_strerror(ret));                  \
        return tcclSystemError;                                                               \
    }                                                                                         \
    return tcclSuccess;

tcclResult_t wrap_sharp_coll_comm_init(sharp_coll_context *context, sharp_coll_comm_init_spec *spec,
                                       sharp_coll_comm **sharp_coll_comm) {
    SHARP_COLL_SUCCESSNO_CHECK(sharpSymbols, sharp_coll_comm_init, sharp_coll_comm_init(context, spec, sharp_coll_comm),
                               0, "sharp_coll_comm_init");
}

tcclResult_t wrap_sharp_coll_comm_destroy(sharp_coll_comm *comm) {
    SHARP_COLL_SUCCESSNO_CHECK(sharpSymbols, sharp_coll_comm_destroy, sharp_coll_comm_destroy(comm), 0,
                               "sharp_coll_comm_destroy");
}

tcclResult_t wrap_sharp_coll_init(sharp_coll_init_spec *sharp_coll_spec, sharp_coll_context **sharp_coll_context) {
    SHARP_COLL_SUCCESSNO_CHECK(sharpSymbols, sharp_coll_init, sharp_coll_init(sharp_coll_spec, sharp_coll_context), 0,
                               "sharp_coll_init");
}

tcclResult_t wrap_sharp_coll_reg_mr(sharp_coll_context *context, void *buf, size_t size, void **mr) {
    SHARP_COLL_SUCCESSNO_CHECK(sharpSymbols, sharp_coll_reg_mr, sharp_coll_reg_mr(context, buf, size, mr), 0,
                               "sharp_coll_reg_mr");
}

const char *wrap_sharp_coll_strerror(int error) {
    if (sharpSymbols.sharp_coll_strerror == NULL) {
        return NULL;
    }
    return sharpSymbols.sharp_coll_strerror(error);
}

tcclResult_t wrap_sharp_coll_do_bcast(sharp_coll_comm *comm, sharp_coll_bcast_spec *spec) {
    SHARP_COLL_SUCCESSNO_CHECK(sharpSymbols, sharp_coll_do_bcast, sharp_coll_do_bcast(comm, spec), 0,
                               "sharp_coll_do_bcast");
}

tcclResult_t wrap_sharp_coll_dereg_mr(sharp_coll_context *context, void *mr) {
    SHARP_COLL_SUCCESSNO_CHECK(sharpSymbols, sharp_coll_dereg_mr, sharp_coll_dereg_mr(context, mr), 0,
                               "sharp_coll_dereg_mr");
}

tcclResult_t wrap_sharp_coll_finalize(sharp_coll_context *context) {
    SHARP_COLL_SUCCESSNO_CHECK(sharpSymbols, sharp_coll_finalize, sharp_coll_finalize(context), 0,
                               "sharp_coll_finalize");
}

sharp_error_no wrap_sharp_coll_do_allreduce(sharp_coll_comm *comm, sharp_coll_reduce_spec *spec) {
    if (sharpSymbols.sharp_coll_do_allreduce == NULL) {
        return SHARP_COLL_ERROR;
    }
    return sharpSymbols.sharp_coll_do_allreduce(comm, spec);
}

sharp_coll_config *wrap_default_sharp_coll_config() { return sharpSymbols.sharp_coll_default_config; }

tcclResult_t wrap_sharp_coll_caps_query(struct sharp_coll_context *context, struct sharp_coll_caps *sharp_caps) {
    SHARP_COLL_SUCCESSNO_CHECK(sharpSymbols, sharp_coll_caps_query, sharp_coll_caps_query(context, sharp_caps), 0,
                               "sharp_coll_caps_query");
}

tcclResult_t wrap_sharp_coll_reg_mr_v2(struct sharp_coll_context *context, void *buf, size_t size,
                                       const struct sharp_coll_reg_params *params, void **mr) {
    SHARP_COLL_SUCCESSNO_CHECK(sharpSymbols, sharp_coll_reg_mr_v2, sharp_coll_reg_mr_v2(context, buf, size, params, mr),
                               0, "sharp_coll_reg_mr_v2");
}

tcclResult_t wrap_sharp_coll_do_allreduce_nb(struct sharp_coll_comm *comm, struct sharp_coll_reduce_spec *spec,
                                             void **handle) {
    SHARP_COLL_SUCCESSNO_CHECK(sharpSymbols, sharp_coll_do_allreduce_nb, sharp_coll_do_allreduce_nb(comm, spec, handle),
                               0, "sharp_coll_do_allreduce_nb");
}

tcclResult_t wrap_sharp_coll_req_test(void *handle) {
    SHARP_COLL_SUCCESSNO_CHECK(sharpSymbols, sharp_coll_req_test, sharp_coll_req_test(handle), 0,
                               "sharp_coll_req_test");
}

tcclResult_t wrap_sharp_coll_req_free(void *handle) {
    SHARP_COLL_SUCCESSNO_CHECK(sharpSymbols, sharp_coll_req_free, sharp_coll_req_free(handle), 0,
                               "sharp_coll_req_free");
}

#undef CHECK_NOT_NULL
#undef SHARP_COLL_ERRNO_CHECK
#undef SHARP_COLL_SUCCESSNO_CHECK
#undef SHARP_COLL_RETVAL_CHECK
