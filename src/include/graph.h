#ifndef TCCL_GRAPH_H
#define TCCL_GRAPH_H

#include <sdaa_runtime.h>
#include <tccl.h>

#include <iostream>
#include <vector>

#include "attributes.h"
#include "log.h"

#include "yangl.h" //by-yl
/*Generate Fake Cross Addr*/
#define CROSS_VIRT_BASE (0x630000000000ULL)
#define CROSS_VIRT_SHIFT (32)

enum class tcclGraphNode_t {
    Kernel,
    ResideKernel,
    IpcP2p,
    MemOps,
};

class GraphManager {
 public:
    explicit GraphManager(const std::vector<tcclGraphNode_t> &nodes) {
        /*Create Graph*/
        SDAACHECK(sdaaGraphCreate(&graph_, 0));
        /*Add Fake Node*/
        sdaaGraphNode_t *depends = nullptr;

        nodes_.resize(nodes.size());
        for (size_t i = 0; i < nodes.size(); i++) {
            switch (nodes[i]) {
                case tcclGraphNode_t::Kernel: {
                    addEmptyKernelNode(i, depends);
                } break;
                case tcclGraphNode_t::ResideKernel: {
                    addEmptyResideKernelNode(i, depends);
                } break;
                case tcclGraphNode_t::IpcP2p: {
                    addEmptyP2pNode(i, depends);
                } break;
                case tcclGraphNode_t::MemOps: {
                    addEmptyMemOpsNode(i, depends);
                } break;
                default: {
                    printf("Unkown Node :%d\n", (int)nodes[i]);
                }
            }
            depends = &nodes_[i];
        }
        /*Instantiat*/
        SDAACHECK(sdaaGraphInstantiate(&exec_, graph_, 0));
    }

    ~GraphManager() { SDAACHECK(sdaaGraphDestroy(graph_)); }

    TCCL_ALWAYS_INLINE_HOT tcclResult_t beginModifyGraph() noexcept {
        tcclGraphIndex_ = 0;
        return tcclSuccess;
    }

    TCCL_ALWAYS_INLINE_HOT tcclResult_t endModifyGraph() noexcept {
        /*All node should be modify!*/
        if (tcclGraphIndex_ != nodes_.size()) {
            return tcclInvalidUsage;
        }
        tcclGraphIndex_ = 0;
        return tcclSuccess;
    }

    TCCL_ALWAYS_INLINE_HOT tcclResult_t updateGraphKernelNode(void *func, void **kernelParams, void **extra) noexcept {
        struct sdaaKernelNodeParams param = {func, kernelParams, extra};
        SDAACHECK(sdaaGraphExecKernelNodeSetParams(exec_, nodes_[tcclGraphIndex_++], &param));
        return tcclSuccess;
    }

    TCCL_ALWAYS_INLINE_HOT tcclResult_t updateGraphResideKernelNode(void *func, void **kernelParams, void **extra) noexcept {
        struct sdaaKernelNodeParams param = {func, kernelParams, extra};
        SDAACHECK(sdaaGraphExecKernelNodeSetParams(exec_, nodes_[tcclGraphIndex_++], &param));
        return tcclSuccess;
    }

    TCCL_ALWAYS_INLINE_HOT tcclResult_t updateGraphP2pNode(void *src_vaddr, void *dst_vaddr, size_t ByteCount,
                                                           int RelaxOrderFlag) noexcept {
        struct sdaaP2pcpyNodeParams param = {src_vaddr, dst_vaddr, ByteCount, RelaxOrderFlag};

        SDAACHECK(sdaaGraphExecP2pcpyNodeSetParams(exec_, nodes_[tcclGraphIndex_++], &param));
        return tcclSuccess;
    }

    TCCL_ALWAYS_INLINE_HOT tcclResult_t updateGraphMemOpsNode(sdaaStreamOpType type, void *addr, uint64_t value,
                                                              unsigned int flags) noexcept {
        struct sdaaMemOpNodeParams param = {type, addr, value, flags};
        SDAACHECK(sdaaGraphExecMemOpNodeSetParams(exec_, nodes_[tcclGraphIndex_++], &param));

        return tcclSuccess;
    }

    TCCL_ALWAYS_INLINE_HOT tcclResult_t launchGraph(sdaaStream_t stream) noexcept {
        SDAACHECK(sdaaGraphLaunch(exec_, stream));
        return tcclSuccess;
    }

 private:
    uint64_t cross_addr(int card_id) { return (CROSS_VIRT_BASE | (((uint64_t)card_id) << CROSS_VIRT_SHIFT)); }

    void addEmptyKernelNode(int index, const sdaaGraphNode_t *depends);
    void addEmptyResideKernelNode(int index, const sdaaGraphNode_t *depends);

    void addEmptyP2pNode(int index, const sdaaGraphNode_t *depends) {
        struct sdaaP2pcpyNodeParams param;
        param.src_vaddr = (void *)cross_addr(0);
        param.dst_vaddr = (void *)cross_addr(1);
        param.ByteCount = 4;
        param.RelaxOrderFlag = 0;
        size_t numDependencies = (index == 0) ? 0 : 1;
        SDAACHECK(sdaaGraphAddP2pcpyNode(&nodes_[index], graph_, depends, numDependencies, &param));
    }

    void addEmptyMemOpsNode(int index, const sdaaGraphNode_t *depends) {
        struct sdaaMemOpNodeParams param;
        param.type = SDAA_STREAM_MEM_OP_SET_VALUE64;
        param.addr = (void *)cross_addr(0);
        param.value = 0;
        param.flags = 0;
        size_t numDependencies = (index == 0) ? 0 : 1;
        SDAACHECK(sdaaGraphAddMemOpNode(&nodes_[index], graph_, depends, numDependencies, &param));
    }

    sdaaGraph_t graph_{nullptr};
    sdaaGraphExec_t exec_{nullptr};
    std::vector<sdaaGraphNode_t> nodes_;
    size_t tcclGraphIndex_{0};
};

#define tcclGraphP2pNode(src_vaddr, dst_vaddr, ByteCount, RelaxOrderFlag)                                      \
    do {                                                                                                       \
        if (comm->fp) {                                                                                        \
            fprintf(comm->fp, "%s:%d dst:%p, src:%p, size:%ld, flag:%d, port:%d,%d\n", __FUNCTION__, __LINE__, \
                    src_vaddr, dst_vaddr, (uint64_t)ByteCount, RelaxOrderFlag, comm->port, comm->optcnt);      \
        }                                                                                                      \
        graph_manager->updateGraphP2pNode(src_vaddr, dst_vaddr, ByteCount, RelaxOrderFlag);                    \
    } while (0)

#define tcclGraphMemOpsNode(type, addr, value, flags)                                                                 \
    do {                                                                                                              \
        if (comm->fp) {                                                                                               \
            fprintf(comm->fp, "%s:%d type:%d, addr:%p, val:%ld, flag:%d, port:%d,%d\n", __FUNCTION__, __LINE__, type, \
                    addr, (uint64_t)value, flags, comm->port, comm->optcnt);                                          \
        }                                                                                                             \
        graph_manager->updateGraphMemOpsNode(type, addr, value, flags);                                               \
    } while (0)

#define tcclLaunchGraph(stream)                                                                             \
    do {                                                                                                    \
        if (comm->fp) {                                                                                     \
            fprintf(comm->fp, "%s:%d launch graph stream:%p, port:%d,%d\n", __FUNCTION__, __LINE__, stream, \
                    comm->port, comm->optcnt);                                                              \
        }                                                                                                   \
        graph_manager->launchGraph(stream);                                                                 \
    } while (0)

#endif
