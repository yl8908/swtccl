#ifndef COMM_KERNEL_H
#define COMM_KERNEL_H

#include "tccl.h"

//using namespace sdaa;

#define __slave_dma __builtin_sw_slave_athread_dma


#define DEVICE_KERNEL_CHOOSE(A, B, C) device_kernel_calculate(A, B, C, count, type, op)
#endif
