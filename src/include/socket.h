/*************************************************************************
 * Copyright (c) 2016-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef TCCL_SOCKET_H_
#define TCCL_SOCKET_H_

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>

#include "tccl.h"

#define MAX_IFS 16
#define MAX_IF_NAME_SIZE 16
#define SLEEP_INT 1000           // connection retry sleep interval in usec
#define RETRY_REFUSED_TIMES 2e4  // connection refused retry times before reporting a timeout (20 sec)
#define RETRY_TIMEDOUT_TIMES 3   // connection timed out retry times (each one can take 20s)
#define SOCKET_NAME_MAXLEN (NI_MAXHOST + NI_MAXSERV)

/* Common socket address storage structure for IPv4/IPv6 */
union tcclSocketAddress {
    struct sockaddr sa;
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
};

enum tcclSocketState {
    tcclSocketConnecting = 0,
    tcclSocketConnected  = 1,
    tcclSocketError      = 2,
    tcclSocketStateNum   = 3
};

struct tcclSocket {
    int fd;
    union tcclSocketAddress addr;
    volatile uint32_t* abortFlag;
    int asyncFlag;
    enum tcclSocketState state;
};

const char* tcclSocketToString(union tcclSocketAddress* addr, char* buf, const int numericHostForm = 1);
int tcclFindInterfaces(char* ifNames, union tcclSocketAddress* ifAddrs, int ifNameMaxSize, int maxIfs);
tcclResult_t tcclSocketListen(struct tcclSocket* sock);
tcclResult_t tcclSocketConnect(struct tcclSocket* sock);
tcclResult_t tcclSocketAccept(struct tcclSocket* sock, struct tcclSocket* listenSocket);

#define TCCL_SOCKET_SEND 0
#define TCCL_SOCKET_RECV 1

tcclResult_t tcclSocketSend(struct tcclSocket* sock, void* ptr, int size);
tcclResult_t tcclSocketRecv(struct tcclSocket* sock, void* ptr, int size);
tcclResult_t tcclSocketInit(struct tcclSocket* sock, union tcclSocketAddress* addr = NULL,
                            volatile uint32_t* abortFlag = NULL, int asyncFlag = 0);
#endif
