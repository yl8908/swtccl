#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "shm.h"
#include "comm.h"

char g_shmDir[TCCL_SHM_PATH_LEN] = { 0 };

void tcclShmDirInit(void) {
    const char *env = getenv("TCCL_SHM_PATH");
    if ((env == NULL) || (strlen(env) > TCCL_SHM_PATH_LEN) || (strlen(env) == 0)) {
        (void)snprintf(g_shmDir, sizeof(g_shmDir), "/tmp/tcclshm-%d", getuid());
        (void)mkdir(g_shmDir, S_IRWXU | S_IRWXG | S_IRWXO);
    } else {
        (void)snprintf(g_shmDir, sizeof(g_shmDir), "%s", env);
    }
    return;
}

void tcclDelShmFile(int port) {
    char command[TCCL_SHM_TOTAL_LEN] = { 0 };
    (void)snprintf(command, sizeof(command), "rm -rf %s/tcclShm-%d*", g_shmDir, port);
    (void)system(command);
    return;
}

bool tcclIsShmExist(char *path, int port, int size) {
    bool isExist;
    char filename[TCCL_SHM_TOTAL_LEN] = { 0 };

    if (g_shmDir[0] == '\0') {
        tcclShmDirInit();
    }
    (void)snprintf(filename, sizeof(filename), "%s/tcclShm-%d-%s", g_shmDir, port, path);

    int fd = open(filename, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        isExist = true;
    } else {
        close(fd);
        isExist = false;
    }
    return isExist;
}

void *tcclShmAlloc(char *path, int port, int size) {
    char filename[TCCL_SHM_TOTAL_LEN] = { 0 };

    if (g_shmDir[0] == '\0') {
        tcclShmDirInit();
    }
    (void)snprintf(filename, sizeof(filename), "%s/tcclShm-%d-%s", g_shmDir, port, path);

    int fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        printf("open error, errno:%d(%s), filename:%s!!!\n", errno, strerror(errno), filename);
        return NULL;
    }
    ftruncate(fd, size);

    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (addr == MAP_FAILED) {
        printf("mmap error, errno:%d(%s)!!!\n", errno, strerror(errno));
        return NULL;
    }
    return addr;
}

tcclResult_t tcclShmFree(void *addr, int size) {
    int ret = munmap(addr, size);
    if (ret != 0) {
        printf("munmap error, errno:%d(%s)!!!\n", errno, strerror(errno));
        return tcclSystemError;
    }
    return tcclSuccess;
}

tcclResult_t tcclShmAddrInit(tcclComm_t comm) {
    int node_id = comm->node->node_id;
    int card_id = comm->node->card_id;
    int len = 4 * DEV_NUM * sizeof(void *);
    char name[TCCL_SHM_NAME_LEN];
    (void)snprintf(name, sizeof(name), "ShmAddr-%d-%d", node_id, card_id);
    void *addr = tcclShmAlloc(name, comm->port, len);
    if (addr == NULL) {
        printf("mmap error!\n");
        return tcclSystemError;
    }
    comm->list = (void **)addr;
    return tcclSuccess;
}
