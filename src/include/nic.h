#ifndef TCCL_NIC_H
#define TCCL_NIC_H

#include <iostream>
#include <thread>
#include <vector>

#include "log.h"
#include "proc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PS() start_time = std::chrono::high_resolution_clock::now();

#define PE(A)                                                    \
    end_time = std::chrono::high_resolution_clock::now();        \
    elapsed_time = end_time - start_time;                        \
    if (A && myrank == 0) {                                      \
        TCCL_DEBUG_LOG("%s : %f ms\n", A, elapsed_time.count()); \
    }

/* Nic rdma resource */
struct nic_res {
    uint64_t bar_addr;
    uint64_t pci_name;
    uint64_t node_guid;
    sdaaNicRdmaQueue_t rdma_queue;
};

/* Creates nic rdma resource */
struct nic_res* nic_init(int rank);
/* Init nic rdma info */
tcclResult_t tcclNicInit(struct nic_res** res, int card_id);
/* Clean nic rdma resources(except one card situation) */
tcclResult_t tcclNicFini(tcclComm_t comm);
/* Check if self & peer in same node */
bool tcclNicIsSameNode(int self_node_id, int peer_node_id);
/* Check if self & peer in same pcie switch */
bool tcclNicIsSameSwitch(int self_card_id, int peer_card_id);

#ifdef __cplusplus
}
#endif

#endif
