/*************************************************************************
 * Copyright (c) 2016-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef XCCL_SOCKET_H_
#define XCCL_SOCKET_H_

#include "xccl.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#include "stdbool.h"
#include "utils.h"

#define MAX_IFS 16
#define MAX_IF_NAME_SIZE 16
#define SLEEP_INT            1000 // connection retry sleep interval in usec
#define RETRY_REFUSED_TIMES   2e4 // connection refused retry times before reporting a timeout (20 sec)
#define RETRY_TIMEDOUT_TIMES    3 // connection timed out retry times (each one can take 20s)
#define SOCKET_NAME_MAXLEN (NI_MAXHOST+NI_MAXSERV)

/* Common socket address storage structure for IPv4/IPv6 */
union xcclSocketAddress {
  struct sockaddr sa;
  struct sockaddr_in sin;
  struct sockaddr_in6 sin6;
};

enum xcclSocketState {
  xcclSocketConnecting = 0,
  xcclSocketConnected = 1,
  xcclSocketError = 2,
  xcclSocketStateNum = 3
} ;

struct xcclSocket {
  int fd;
  union xcclSocketAddress addr;
  volatile uint32_t* abortFlag;
  int asyncFlag;
  enum xcclSocketState state;
};

const char *xcclSocketToString(union xcclSocketAddress *addr, char *buf, const int numericHostForm);
xcclResult_t xcclGetSocketAddrFromString(union xcclSocketAddress* ua, const char* ip_port_pair);
int xcclFindInterfaceMatchSubnet(char* ifNames, union xcclSocketAddress* localAddrs, union xcclSocketAddress* remoteAddr, int ifNameMaxSize, int maxIfs);
int xcclFindInterfaces(char* ifNames, union xcclSocketAddress *ifAddrs, int ifNameMaxSize, int maxIfs);
// Create a listening socket. sock->addr can be pre-filled with IP & port info. sock->fd is set after a successful call
xcclResult_t xcclSocketListen(struct xcclSocket* sock);
// Connect to sock->addr. sock->fd is set after a successful call.
xcclResult_t xcclSocketConnect(struct xcclSocket* sock);
// Return socket connection state.
xcclResult_t xcclGetSocketState(struct xcclSocket* sock, enum xcclSocketState* state);
// Accept an incoming connection from listenSocket->fd and keep the file descriptor in sock->fd, with the remote side IP/port in sock->addr.
xcclResult_t xcclSocketAccept(struct xcclSocket* sock, struct xcclSocket* listenSocket);

#define XCCL_SOCKET_SEND 0
#define XCCL_SOCKET_RECV 1

xcclResult_t xcclSocketProgress(int op, struct xcclSocket* sock, void* ptr, int size, int* offset);
xcclResult_t xcclSocketWait(int op, struct xcclSocket* sock, void* ptr, int size, int* offset);
xcclResult_t xcclSocketSend(struct xcclSocket* sock, void* ptr, int size);
xcclResult_t xcclSocketRecv(struct xcclSocket* sock, void* ptr, int size);
xcclResult_t xcclSocketTryRecv(struct xcclSocket* sock, void* ptr, int size, int* closed);
/* initialize a socket. */
xcclResult_t xcclSocketInit(struct xcclSocket* sock, union xcclSocketAddress* addr, volatile uint32_t* abortFlag, int asyncFlag);
#endif
