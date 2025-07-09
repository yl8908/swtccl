#include "collectives.h"
#include "common_kernel.h"

template <typename T>
inline void host_calculate(void *in0, void *in1, void *out, size_t count, tcclRedOp_t op) {
    T *a = (T *)in0;
    T *b = (T *)in1;
    T *c = (T *)out;

    switch (op) {
        case tcclSum:
            for (int i = 0; i < count; i++) c[i] = a[i] + b[i];
            break;
        case tcclProd:
            for (int i = 0; i < count; i++) c[i] = a[i] * b[i];
            break;
        case tcclMax:
            for (int i = 0; i < count; i++) c[i] = a[i] > b[i] ? a[i] : b[i];
            break;
        case tcclMin:
            for (int i = 0; i < count; i++) c[i] = a[i] < b[i] ? a[i] : b[i];
            break;
        default:
            printf("TCCL don't support this op:%d\n", op);
            break;
    }
    return;
}

template <typename T, typename B>
inline void host_SMID_calculate(void *in0, void *in1, void *out, size_t count, tcclRedOp_t op) {
    T *a = (T *)in0;
    T *b = (T *)in1;
    T *c = (T *)out;

    B *av16 = (B *)in0;
    B *bv16 = (B *)in1;
    B *cv16 = (B *)out;

    switch (op) {
        case tcclSum:
            for (int i = 0; i < (count / 16); i++) cv16[i] = av16[i] + bv16[i];
            break;
        case tcclProd:
            for (int i = 0; i < count; i++) c[i] = a[i] * b[i];
            break;
        case tcclMax:
            for (int i = 0; i < count; i++) c[i] = a[i] > b[i] ? a[i] : b[i];
            break;
        case tcclMin:
            for (int i = 0; i < count; i++) c[i] = a[i] < b[i] ? a[i] : b[i];
            break;
        default:
            printf("TCCL don't support this op:%d\n", op);
            break;
    }
    return;
}

void host_calculate(void *in0, void *in1, void *out, size_t count, tcclDataType_t datatype, tcclRedOp_t op) {
    switch (datatype) {
        case tcclInt8:
            host_calculate<int8_t>(in0, in1, out, count, op);
            break;
        case tcclUint8:
            host_calculate<uint8_t>(in0, in1, out, count, op);
            break;
        case tcclInt:
            host_calculate<int>(in0, in1, out, count, op);
            break;
        case tcclUint:
            host_calculate<uint>(in0, in1, out, count, op);
            break;
        case tcclFloat:
            if (count & 15)
                host_calculate<float>(in0, in1, out, count, op);
            else
                //host_SMID_calculate<float, floatv16>(in0, in1, out, count, op);
            break;
        case tcclDouble:
            host_calculate<double>(in0, in1, out, count, op);
            break;
        case tcclInt64:
            host_calculate<int64_t>(in0, in1, out, count, op);
            break;
        case tcclUint64:
            host_calculate<uint64_t>(in0, in1, out, count, op);
            break;
        case tcclHalf:
            //host_calculate<half>(in0, in1, out, count, op);
            break;
        default:
            printf("TCCL don't support this datatype:%d\n", datatype);
            break;
    }
    return;
}

int tcclsizeof_datatype(tcclDataType_t datatype) {
    switch (datatype) {
        case tcclHalf:
            //return sizeof(half);
        case tcclBF16:
            //return sizeof(half);
        case tcclInt8:
            return sizeof(int8_t);
        case tcclUint8:
            return sizeof(uint8_t);
        case tcclInt:
            return sizeof(int);
        case tcclUint:
            return sizeof(uint);
        case tcclFloat:
            return sizeof(float);
        case tcclDouble:
            return sizeof(double);
        case tcclInt64:
            return sizeof(int64_t);
        case tcclUint64:
            return sizeof(uint64_t);
        default:
            return 0;
    }
}
