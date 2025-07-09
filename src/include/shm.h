#ifndef SHM_H
#define SHM_H

#include "tccl.h"

#define TCCL_SHM_PATH_LEN 128
#define TCCL_SHM_NAME_LEN 128
#define TCCL_SHM_TOTAL_LEN (TCCL_SHM_PATH_LEN + TCCL_SHM_NAME_LEN)

void tcclShmDirInit(void);
void tcclDelShmFile(int port);
bool tcclIsShmExist(char *path, int port, int size);
void *tcclShmAlloc(char *path, int port, int size);
tcclResult_t tcclShmFree(void *addr, int size);
tcclResult_t tcclShmAddrInit(tcclComm_t comm);
#endif
