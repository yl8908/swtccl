#ifndef TCCL_ARGCHECK_H
#define TCCL_ARGCHECK_H

#include <stdio.h>

#include "tccl.h"

tcclResult_t PtrCheck(void* ptr, const char* opname, const char* ptrname) {
    if (ptr == NULL) {
        printf("%s : %s argument is NULL\n", opname, ptrname);
        return tcclInvalidArgument;
    }
    return tcclSuccess;
}

#endif
