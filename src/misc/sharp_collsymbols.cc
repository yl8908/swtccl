#include "sharp_collsymbols.h"

#include "comm.h"
#include "dlfcn.h"

#define SHARP_COLL_VERSION "SHARP_COLL_5"

tcclResult_t buildSharpCollSymbols(struct tcclSharpCollSymbols* sharpSymbols) {
    const char* use_sharp = getenv("TCCL_USE_SHARP");
    if (use_sharp != NULL && strcmp(use_sharp, "0") == 0) {
        return tcclSuccess;
    }
    static void* sharphandle = NULL;
    void** cast;
    void* tmp;

    if (use_sharp == NULL || strcmp(use_sharp, "1") == 0) {
        sharphandle = dlopen("libsharp_coll.so", RTLD_NOW);
        if (!sharphandle) {
            sharphandle = dlopen("libsharp_coll.so.5", RTLD_NOW);
            if (!sharphandle) {
                INFO(NCCL_INIT, "Failed to open libsharp_coll.so");
                goto teardown;
            }
        }
    } else {
        sharphandle = dlopen(use_sharp, RTLD_NOW);
        if (!sharphandle) {
            goto teardown;
        }
    }

#define LOAD_SYM(handle, symbol, funcptr)                                                         \
    do {                                                                                          \
        cast = (void**)&funcptr;                                                                  \
        tmp = dlsym(handle, symbol);                                                              \
        if (tmp == NULL) {                                                                        \
            WARN("dlvsym failed on %s - %s version %s\n", symbol, dlerror(), SHARP_COLL_VERSION); \
            goto teardown;                                                                        \
        }                                                                                         \
        *cast = tmp;                                                                              \
    } while (0)

// Attempt to load a specific symbol version - fail silently
#define LOAD_SYM_VERSION(handle, symbol, funcptr, version) \
    do {                                                   \
        cast = (void**)&funcptr;                           \
        *cast = dlvsym(handle, symbol, version);           \
    } while (0)

    LOAD_SYM(sharphandle, "sharp_coll_comm_init", sharpSymbols->sharp_coll_comm_init);
    LOAD_SYM(sharphandle, "sharp_coll_comm_destroy", sharpSymbols->sharp_coll_comm_destroy);
    LOAD_SYM(sharphandle, "sharp_coll_init", sharpSymbols->sharp_coll_init);
    LOAD_SYM(sharphandle, "sharp_coll_reg_mr", sharpSymbols->sharp_coll_reg_mr);
    LOAD_SYM(sharphandle, "sharp_coll_strerror", sharpSymbols->sharp_coll_strerror);
    LOAD_SYM(sharphandle, "sharp_coll_do_bcast", sharpSymbols->sharp_coll_do_bcast);
    LOAD_SYM(sharphandle, "sharp_coll_dereg_mr", sharpSymbols->sharp_coll_dereg_mr);
    LOAD_SYM(sharphandle, "sharp_coll_comm_destroy", sharpSymbols->sharp_coll_comm_destroy);
    LOAD_SYM(sharphandle, "sharp_coll_finalize", sharpSymbols->sharp_coll_finalize);
    LOAD_SYM(sharphandle, "sharp_coll_do_allreduce", sharpSymbols->sharp_coll_do_allreduce);
    LOAD_SYM(sharphandle, "sharp_coll_default_config", sharpSymbols->sharp_coll_default_config);

    return tcclSuccess;
teardown:
    sharpSymbols->sharp_coll_comm_destroy = NULL;
    sharpSymbols->sharp_coll_init = NULL;
    sharpSymbols->sharp_coll_comm_init = NULL;
    sharpSymbols->sharp_coll_reg_mr = NULL;
    sharpSymbols->sharp_coll_strerror = NULL;
    sharpSymbols->sharp_coll_do_bcast = NULL;
    sharpSymbols->sharp_coll_dereg_mr = NULL;
    sharpSymbols->sharp_coll_comm_destroy = NULL;
    sharpSymbols->sharp_coll_finalize = NULL;
    sharpSymbols->sharp_coll_do_allreduce = NULL;
    sharpSymbols->sharp_coll_default_config = NULL;
    if (sharphandle) {
        dlclose(sharphandle);
        sharphandle = NULL;
    }
    return tcclSystemError;
}
