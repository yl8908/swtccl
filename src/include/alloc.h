#ifndef TCCL_ALLOC_H
#define TCCL_ALLOC_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h> //by-yl
#include "tccl.h"

template <typename T>
static tcclResult_t tcclCalloc(T **ptr, size_t nelem) {
    T *p = (T *)malloc(nelem * sizeof(T));
    if (p == NULL) {
        printf("Failed to malloc %ld bytes\n", nelem * sizeof(T));
        return tcclSystemError;
    }
    memset((void *)p, 0, nelem * sizeof(T));
    *ptr = p;
    return tcclSuccess;
}

template <typename T>
static tcclResult_t tcclFree(T *ptr) {
    //free(ptr);
    free((void *)ptr);  //by-yl
    return tcclSuccess;
}

#endif
