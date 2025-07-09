#ifndef UTIL_LOG_H_
#define UTIL_LOG_H_
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

static const char *file_name(const char *str) { return strrchr(str, '/') ? strrchr(str, '/') + 1 : str; }

#define tccl_print(str, fmt, args...)                                                 \
    do {                                                                              \
        printf("[%s][%s:%d]: " fmt "\n", str, file_name(__FILE__), __LINE__, ##args); \
    } while (0)

#define TCCL_LOG (getenv("TCCL_LOG"))

#define TCCL_ERROR_LOG(msg...) tccl_print("ERROR", ##msg)
#define TCCL_DEBUG_LOG(msg...)                    \
    do {                                          \
        if (TCCL_LOG) tccl_print("DEBUG", ##msg); \
    } while (0)

#ifdef __cplusplus
}
#endif /* __cplusplus  */

#endif /* UTIL_LOG_H_ */
