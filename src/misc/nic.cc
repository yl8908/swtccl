#include "nic.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <iostream>
#include <tuple>
#include <utility>
#include <vector>

#include "collectives.h"
#include "debug.h"
//#include "sdaa_driver_api_yl.h"  //by-yl
#include "sdaa_runtime.h"
#include "socket.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PRINTF
#define DEPTH 1024

/* A global vector which saves rdma info:
   tuple<pci_name,node_guid,bar_addr> */
static std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> nic_card_info;

//* Correspondence between AI phy_card_id and N2 pci_name
uint64_t dev_pci_name[8] = {0x08, 0x0d, 0x1b, 0x18, 0x3c, 0x39, 0x4f, 0x50};

int init_nic_card_info() {
    int i = 0;
    // File paths where nic info is saved
    const char* infotopdir = "/sys/class/swnet";
    const char* node_infomation = "node_guid";
    const char* bar_infomation = "device/resource";
    const char* pci_infomation = "pci_name";
    char infofname[512] = {0};
    char line[4096] = {0};
    DIR* dir = NULL;
    struct dirent* filename = NULL;
    struct nic_device_info* infos = NULL;
    uint64_t pci_name = 0, node_guid = 0, bar_addr = 0;

    dir = opendir(infotopdir);
    if (NULL == dir) {
        printf("read device infomation failed, dir %s not exist!\n", infotopdir);
        return -1;
    }

    while ((filename = readdir(dir)) != NULL) {
        if ((0 == strcmp(".", filename->d_name)) || (0 == strcmp("..", filename->d_name))) continue;

        FILE* f;

        // Read node_guid
        snprintf(infofname, sizeof(infofname), "%s/%s/%s", infotopdir, filename->d_name, node_infomation);
        f = fopen(infofname, "r");
        if (NULL == f) {
            printf("open information %s failed\n", infofname);
            continue;
        }
        memset(line, 0, sizeof(line));
        fgets(line, sizeof(line), f);
        node_guid = strtoull(line, NULL, 16);
        fclose(f);

        // Read nic bar address
        snprintf(infofname, sizeof(infofname), "%s/%s/%s", infotopdir, filename->d_name, bar_infomation);
        f = fopen(infofname, "r");
        if (NULL == f) {
            printf("open information %s failed\n", infofname);
            continue;
        }
        memset(line, 0, sizeof(line));
        fgets(line, sizeof(line), f);

        // Get first token by strtok_r()
        char* token = NULL;
        char* saveptr = NULL;
        token = strtok_r(line, " ", &saveptr);
        if (token != NULL) {
            // Convert token to longlong integer using strtoull()
            bar_addr = strtoull(token, NULL, 16);
        } else {
            printf("get bar_addr failed\n");
        }
        fclose(f);

        // Read nic pci_name
        snprintf(infofname, sizeof(infofname), "%s/%s/%s", infotopdir, filename->d_name, pci_infomation);
        f = fopen(infofname, "r");
        if (NULL == f) {
            printf("open information %s failed\n", infofname);
            continue;
        }
        memset(line, 0, sizeof(line));
        fgets(line, sizeof(line), f);
        pci_name = strtoull(strchr(line, ':') + 1, NULL, 16);
        fclose(f);

        i++;
        nic_card_info.push_back(std::make_tuple(pci_name, node_guid, bar_addr));
    }

    closedir(dir);
    return 0;
}

// Read guid by pci from global vector
uint64_t get_nic_guid(uint64_t pci_name) {
    for (auto& item : nic_card_info) {
        if (std::get<0>(item) == pci_name) {
            return std::get<1>(item);
        }
    }
    printf("Not found guid, pci : %lx\n", pci_name);
    abort();
    return 0;
}

// Read bar by pci from global vector
uint64_t get_nic_bar_addr(uint64_t pci_name) {
    for (auto& item : nic_card_info) {
        if (std::get<0>(item) == pci_name) {
            return std::get<2>(item);
        }
    }
    printf("Not found guid, pci : %lx\n", pci_name);
    abort();
    return 0;
}

/* Init nic information */
struct nic_res* nic_init(int rank) {
    struct nic_res* res = NULL;
    res = (struct nic_res*)malloc(sizeof(struct nic_res));

    if (res == NULL) {
        printf("nic res malloc failed\n");
        abort();
        return NULL;
    }
    memset(res, 0, sizeof(struct nic_res));

    // Get bar_addr, node_guid by pci_name
    init_nic_card_info();
    res->pci_name = dev_pci_name[rank];
    res->node_guid = get_nic_guid(res->pci_name);
    res->bar_addr = get_nic_bar_addr(res->pci_name);

    PRINTF("pci_name[%lx] node guid :%lx bar addr :%lx\n", res->pci_name, res->node_guid, res->bar_addr);
    SDAACHECK(sdaaNicRdmaQueueCreate(&res->rdma_queue, res->bar_addr, DEPTH));

    return res;
}

tcclResult_t tcclNicInit(struct nic_res** resret, int card_id) {
    struct nic_res* res = nic_init(card_id);
    if (res == NULL) {
        printf("swib init failed!!!\n");
        return tcclSystemError;
    }

    *resret = res;
    return tcclSuccess;
}

tcclResult_t tcclNicFini(tcclComm_t comm) {
#ifdef _SWIB_
    if (comm->allRing->allCardNum != 1) {
        SDAACHECK(sdaaNicRdmaQueueDestroy(comm->res->rdma_queue));
    }
#endif

    if (comm->res) free(comm->res);
    comm->res = nullptr;

    return tcclSuccess;
}

bool tcclNicIsSameNode(int self_node_id, int peer_node_id) {
    // TCCL_DEBUG_LOG("tcclNicIsSameNode self:%d, peer:%d\n", self_node_id, peer_node_id);

    if (self_node_id == peer_node_id) {
        // TCCL_DEBUG_LOG("tcclNicIsSameNode!\n");
        return true;
    }

    return false;
}

bool tcclNicIsSameSwitch(int self_card_id, int peer_card_id) {
    // TCCL_DEBUG_LOG("tcclNicIsSameSwitch self:%d, peer:%d\n", self_card_id, peer_card_id);

    int sets[][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}};

    if (self_card_id == peer_card_id) {
        // TCCL_DEBUG_LOG("WARNING: tcclNicIsSameSwitch(): Your input is the same card!");
        return true;
    }

    for (int i = 0; i < 4; ++i) {
        if ((self_card_id >= sets[i][0] && self_card_id <= sets[i][1]) &&
            (peer_card_id >= sets[i][0] && peer_card_id <= sets[i][1])) {
            return true;
        }
    }

    return false;
}

#ifdef __cplusplus
}
#endif
