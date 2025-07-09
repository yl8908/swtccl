/*
 * Copyright (c) 2017-2022, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef XCCL_NET_V5_H_
#define XCCL_NET_V5_H_

typedef xcclNetProperties_v6_t xcclNetProperties_v5_t;
typedef struct {
  // Name of the network (mainly for logs)
  const char* name;
  // Initialize the network.
  xcclResult_t (*init)(xcclDebugLogger_t logFunction);
  // Return the number of adapters.
  xcclResult_t (*devices)(int* ndev);
  // Get various device properties.
  xcclResult_t (*getProperties)(int dev, xcclNetProperties_v5_t* props);
  // Create a receiving object and provide a handle to connect to it. The
  // handle can be up to XCCL_NET_HANDLE_MAXSIZE bytes and will be exchanged
  // between ranks to create a connection.
  xcclResult_t (*listen)(int dev, void* handle, void** listenComm);
  // Connect to a handle and return a sending comm object for that peer.
  // This call must not block for the connection to be established, and instead
  // should return successfully with sendComm == NULL with the expectation that
  // it will be called again until sendComm != NULL.
  xcclResult_t (*connect)(int dev, void* handle, void** sendComm);
  // Finalize connection establishment after remote peer has called connect.
  // This call must not block for the connection to be established, and instead
  // should return successfully with recvComm == NULL with the expectation that
  // it will be called again until recvComm != NULL.
  xcclResult_t (*accept)(void* listenComm, void** recvComm);
  // Register/Deregister memory. Comm can be either a sendComm or a recvComm.
  // Type is either XCCL_PTR_HOST or XCCL_PTR_CUDA.
  xcclResult_t (*regMr)(void* comm, void* data, int size, int type, void** mhandle);
  xcclResult_t (*deregMr)(void* comm, void* mhandle);
  // Asynchronous send to a peer.
  // May return request == NULL if the call cannot be performed (or would block)
  xcclResult_t (*isend)(void* sendComm, void* data, int size, int tag, void* mhandle, void** request);
  // Asynchronous recv from a peer.
  // May return request == NULL if the call cannot be performed (or would block)
  xcclResult_t (*irecv)(void* recvComm, int n, void** data, int* sizes, int* tags, void** mhandles, void** request);
  // Perform a flush/fence to make sure all data received with XCCL_PTR_CUDA is
  // visible to the GPU
  xcclResult_t (*iflush)(void* recvComm, int n, void** data, int* sizes, void** mhandles, void** request);
  // Test whether a request is complete. If size is not NULL, it returns the
  // number of bytes sent/received.
  xcclResult_t (*test)(void* request, int* done, int* sizes);
  // Close and free send/recv comm objects
  xcclResult_t (*closeSend)(void* sendComm);
  xcclResult_t (*closeRecv)(void* recvComm);
  xcclResult_t (*closeListen)(void* listenComm);
} xcclNet_v5_t;


typedef struct {
  // Name of the collective network (mainly for logs)
  const char* name;
  // Initialize the collective network.
  xcclResult_t (*init)(xcclDebugLogger_t logFunction);
  // Return the number of adapters capable of doing collective operations.
  // If ndev returns 0, all other functions might be set to NULL.
  xcclResult_t (*devices)(int* ndev);
  // Get various device properties.
  xcclResult_t (*getProperties)(int dev, xcclNetProperties_v5_t* props);
  // Create a receiving object and provide a handle to connect to it. The
  // handle can be up to XCCL_NET_HANDLE_MAXSIZE bytes and will be exchanged
  // between ranks to create connections.
  xcclResult_t (*listen)(int dev, void* handle, void** listenComm);
  // Create a group for collective operations. handles have been created
  // using listen() above. rank indicates caller's rank in the collective network.
  xcclResult_t (*connect)(void* handles[], int nranks, int rank, void* listenComm, void** collComm);
  // Returns whether a reduction operation on a data type is supported.
  // 1 for supported, 0 otherwise.
  xcclResult_t (*reduceSupport)(xcclDataType_t dataType, xcclRedOp_t redOp, int* supported);
  // Register/Deregister memory. Type is either XCCL_PTR_HOST or XCCL_PTR_CUDA.
  xcclResult_t (*regMr)(void* collComm, void* data, int size, int type, void** mhandle);
  xcclResult_t (*deregMr)(void* collComm, void* mhandle);
  // Performs an asynchronous allreduce operation on the collective group.
  // May return request == NULL if the call cannot be performed (or would block).
  xcclResult_t (*iallreduce)(void* collComm, void* sendData, void* recvData, int count,
      xcclDataType_t dataType, xcclRedOp_t redOp, void* sendMhandle, void* recvMhandle, void** request);
  // Perform a flush/fence to make sure all data received with XCCL_PTR_CUDA is
  // visible to the GPU
  xcclResult_t (*iflush)(void* collComm, void* data, int size, void* mhandle, void** request);
  // Test whether a request is complete. If size is not NULL, it returns the
  // number of bytes sent/received.
  xcclResult_t (*test)(void* request, int* done, int* size);
  // Close and free collective comm objects
  xcclResult_t (*closeColl)(void* collComm);
  xcclResult_t (*closeListen)(void* listenComm);
} xcclCollNet_v5_t;

#endif
