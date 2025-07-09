/*************************************************************************
 * Copyright (c) 2016-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#include "socket.h"

#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils.h"

/* Format a string representation of a (union tcclSocketAddress *) socket address using getnameinfo()
 *
 * Output: "IPv4/IPv6 address<port>"
 */
const char* tcclSocketToString(union tcclSocketAddress* addr, char* buf, const int numericHostForm /*= 1*/) {
    if (buf == NULL || addr == NULL) return NULL;
    struct sockaddr* saddr = &addr->sa;
    if (saddr->sa_family != AF_INET && saddr->sa_family != AF_INET6) {
        buf[0] = '\0';
        return buf;
    }
    char host[NI_MAXHOST], service[NI_MAXSERV];
    /* NI_NUMERICHOST: If set, then the numeric form of the hostname is returned.
     * (When not set, this will still happen in case the node's name cannot be determined.)
     */
    int flag = NI_NUMERICSERV | (numericHostForm ? NI_NUMERICHOST : 0);
    (void)getnameinfo(saddr, sizeof(union tcclSocketAddress), host, NI_MAXHOST, service, NI_MAXSERV, flag);
    sprintf(buf, "%s<%s>", host, service);  // NOLINT
    return buf;
}

static uint16_t socketToPort(union tcclSocketAddress* addr) {
    struct sockaddr* saddr = &addr->sa;
    return ntohs(saddr->sa_family == AF_INET ? addr->sin.sin_port : addr->sin6.sin6_port);
}

/* Allow the user to force the IPv4/IPv6 interface selection */
static int envSocketFamily(void) {
    int family = -1;  // Family selection is not forced, will use first one found
    char* env  = getenv("TCCL_SOCKET_FAMILY");
    if (env == NULL) return family;

    //   INFO(TCCL_ENV, "TCCL_SOCKET_FAMILY set by environment to %s", env);

    if (strcmp(env, "AF_INET") == 0)
        family = AF_INET;  // IPv4
    else if (strcmp(env, "AF_INET6") == 0)
        family = AF_INET6;  // IPv6
    return family;
}

static int findInterfaces(const char* prefixList, char* names, union tcclSocketAddress* addrs, int sock_family,
                          int maxIfNameSize, int maxIfs) {
#ifdef ENABLE_TRACE
    char line[SOCKET_NAME_MAXLEN + 1];
#endif
    struct netIf userIfs[MAX_IFS];
    bool searchNot = prefixList && prefixList[0] == '^';
    if (searchNot) prefixList++;
    bool searchExact = prefixList && prefixList[0] == '=';
    if (searchExact) prefixList++;
    int nUserIfs = parseStringList(prefixList, userIfs, MAX_IFS);

    int found = 0;
    struct ifaddrs *interfaces, *interface;
    getifaddrs(&interfaces);
    for (interface = interfaces; interface && found < maxIfs; interface = interface->ifa_next) {
        if (interface->ifa_addr == NULL) continue;

        /* We only support IPv4 & IPv6 */
        int family = interface->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6) continue;

        /* Allow the caller to force the socket family type */
        if (sock_family != -1 && family != sock_family) continue;

        /* We also need to skip IPv6 loopback interfaces */
        if (family == AF_INET6) {
            struct sockaddr_in6* sa = (struct sockaddr_in6*)(interface->ifa_addr);
            if (IN6_IS_ADDR_LOOPBACK(&sa->sin6_addr)) continue;
        }

        // check against user specified interfaces
        if (!(matchIfList(interface->ifa_name, -1, userIfs, nUserIfs, searchExact) ^ searchNot)) {
            continue;
        }

        // Check that this interface has not already been saved
        // getifaddrs() normal order appears to be; IPv4, IPv6 Global, IPv6 Link
        bool duplicate = false;
        for (int i = 0; i < found; i++) {
            if (strcmp(interface->ifa_name, names + i * maxIfNameSize) == 0) {
                duplicate = true;
                break;
            }
        }

        if (!duplicate) {
            // Store the interface name
            strncpy(names + found * maxIfNameSize, interface->ifa_name, maxIfNameSize);
            // Store the IP address
            int salen = (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
            memcpy(addrs + found, interface->ifa_addr, salen);
            found++;
        }
    }

    freeifaddrs(interfaces);
    return found;
}

int tcclFindInterfaces(char* ifNames, union tcclSocketAddress* ifAddrs, int ifNameMaxSize, int maxIfs) {
    static int shownIfName = 0;
    int nIfs               = 0;
    // Allow user to force the INET socket family selection
    int sock_family = envSocketFamily();
    // User specified interface
    char* env = getenv("TCCL_SOCKET_IFNAME");
    if (env && strlen(env) > 1) {
        // INFO(TCCL_ENV, "TCCL_SOCKET_IFNAME set by environment to %s", env);
        // Specified by user : find or fail
        // if (shownIfName++ == 0) INFO(TCCL_NET, "TCCL_SOCKET_IFNAME set to %s", env);
        nIfs = findInterfaces(env, ifNames, ifAddrs, sock_family, ifNameMaxSize, maxIfs);
    } else {
        // Try to automatically pick the right one
        // Start with IB
        nIfs = findInterfaces("ib", ifNames, ifAddrs, sock_family, ifNameMaxSize, maxIfs);
        // Then look for anything else (but not docker or lo)
        if (nIfs == 0) nIfs = findInterfaces("^docker,lo", ifNames, ifAddrs, sock_family, ifNameMaxSize, maxIfs);
        // Finally look for docker, then lo.
        if (nIfs == 0) nIfs = findInterfaces("docker", ifNames, ifAddrs, sock_family, ifNameMaxSize, maxIfs);
        if (nIfs == 0) nIfs = findInterfaces("lo", ifNames, ifAddrs, sock_family, ifNameMaxSize, maxIfs);
    }
    return nIfs;
}

tcclResult_t tcclSocketListen(struct tcclSocket* sock) {
    /* IPv4/IPv6 support */
    int family = sock->addr.sa.sa_family;
    int salen  = (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    int flags;

    /* Create socket and bind it to a port */
    int fd = socket(family, SOCK_STREAM, 0);
    if (fd == -1) {
        printf("Net : Socket creation failed : %s\n", strerror(errno));
        return tcclSystemError;
    }

    if (socketToPort(&sock->addr)) {
        // Port is forced by env. Make sure we get the port.
        int opt = 1;
#if defined(SO_REUSEPORT)
        SYSCHECK(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)), "setsockopt");
#else
        SYSCHECK(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)), "setsockopt");
#endif
    }

    /* The socket is set non-blocking for OS level, but asyncFlag is used to control
     * blocking and non-blocking behavior in user level. */
    EQCHECK(flags = fcntl(fd, F_GETFL), -1);
    SYSCHECK(fcntl(fd, F_SETFL, flags | O_NONBLOCK), "fcntl");

    // addr port should be 0 (Any port)
    SYSCHECK(bind(fd, &sock->addr.sa, salen), "bind");

    /* Get the assigned Port */
    socklen_t size = salen;
    SYSCHECK(getsockname(fd, &sock->addr.sa, &size), "getsockname");

#ifdef ENABLE_TRACE
    char line[SOCKET_NAME_MAXLEN + 1];
    TRACE(TCCL_INIT | TCCL_NET, "Listening on socket %s", tcclSocketToString(&sock->addr, line));
#endif

    /* Put the socket in listen mode
     * NB: The backlog will be silently truncated to the value in /proc/sys/net/core/somaxconn
     */
    SYSCHECK(listen(fd, 16384), "listen");
    sock->fd = fd;
    return tcclSuccess;
}

tcclResult_t tcclSocketConnect(struct tcclSocket* sock) {
    char line[SOCKET_NAME_MAXLEN + 1];
    /* IPv4/IPv6 support */
    int family = sock->addr.sa.sa_family;
    if (family != AF_INET && family != AF_INET6) {
        printf("Net : connecting to address %s with family %d is neither AF_INET(%d) nor AF_INET6(%d)",
               tcclSocketToString(&sock->addr, line), family, AF_INET, AF_INET6);
        return tcclInternalError;
    }
    int salen = (family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    int flags;

    /* Connect to a hostname / port */
    int fd = socket(family, SOCK_STREAM, 0);
    if (fd == -1) {
        printf("Net : Socket creation failed : %s", strerror(errno));
        return tcclSystemError;
    }

    const int one = 1;
    SYSCHECK(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(int)), "setsockopt");

    /* The socket is set non-blocking for OS level, but asyncFlag is used to control
     * blocking and non-blocking behavior in user level. */
    EQCHECK(flags = fcntl(fd, F_GETFL), -1);
    SYSCHECK(fcntl(fd, F_SETFL, flags | O_NONBLOCK), "fcntl");

    /*  const int bufsize = 128*1024;
      SYSCHECK(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*)&bufsize, sizeof(int)), "setsockopt");
      SYSCHECK(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*)&bufsize, sizeof(int)), "setsockopt");*/

    int ret;
    int timedout_retries = 0;
    int refused_retries  = 0;
retry:
    /* blocking/non-blocking connect() is determined by asyncFlag. */
    ret = connect(fd, &sock->addr.sa, salen);

    if (!sock->asyncFlag) {
        /* blocking socket, need retry if connect fails. */
        if (errno == EINPROGRESS || errno == EAGAIN || errno == EALREADY ||
            (errno == ECONNREFUSED && ++refused_retries < RETRY_REFUSED_TIMES) ||
            (errno == ETIMEDOUT && ++timedout_retries < RETRY_TIMEDOUT_TIMES)) {
            /* check abortFlag as long as we have chance to retry. */
            if (sock->abortFlag && *sock->abortFlag != 0) return tcclInternalError;
            //   if (errno == ECONNREFUSED && refused_retries % 1000 == 0) INFO(TCCL_ALL, "Call to connect returned %s,
            //   retrying", strerror(errno));
            usleep(SLEEP_INT);
            goto retry;
        }

        /* If connect() fails with errno == EAGAIN/EINPROGRESS/ETIMEDOUT, we may want to try connect again.
         * However, it can return EISCONN instead of success which indicates connection is built up in
         * background already. No need to call connect() again. */
        if (ret == 0 || errno == EISCONN) {
            sock->fd = fd;
            return tcclSuccess;
        }
    } else {
        sock->fd = fd;
        return tcclSuccess;
    }

    printf("Net : Connect to %s failed : %s", tcclSocketToString(&sock->addr, line), strerror(errno));
    return tcclSystemError;
}

tcclResult_t tcclSocketAccept(struct tcclSocket* sock, struct tcclSocket* listenSocket) {
    socklen_t socklen = sizeof(struct sockaddr);
    struct pollfd pollfd;
    int tmpFd = sock->fd = -1;
    int pollret;

    pollfd.fd     = listenSocket->fd;
    pollfd.events = POLLIN;
retry:
    if ((pollret = poll(&pollfd, 1, listenSocket->asyncFlag ? 0 : 100)) < 0) {
        return tcclSystemError;
    } else {
        tmpFd = accept(listenSocket->fd, &sock->addr.sa, &socklen);
    }

    if (!listenSocket->asyncFlag) {
        /* blocking socket, if tmpFd is still -1, we need to retry */
        if (tmpFd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (listenSocket->abortFlag && *listenSocket->abortFlag != 0) return tcclInternalError;
            goto retry;
        }
        EQCHECK(tmpFd, -1);
    }

    sock->fd = tmpFd;
    return tcclSuccess;
}

tcclResult_t tcclSocketInit(struct tcclSocket* sock, union tcclSocketAddress* addr, volatile uint32_t* abortFlag,
                            int asyncFlag) {
    if (sock == NULL) return tcclSuccess;

    sock->fd = -1;
    if (addr) {
        memcpy(&sock->addr, addr, sizeof(union tcclSocketAddress));
    } else {
        memset(&sock->addr, 0, sizeof(union tcclSocketAddress));
    }
    sock->abortFlag = abortFlag;
    sock->asyncFlag = asyncFlag;
    sock->state     = tcclSocketStateNum;
    return tcclSuccess;
}

static tcclResult_t tcclSocketProgressOpt(int op, struct tcclSocket* sock, void* ptr, int size, int* offset, int block,
                                          int* closed) {
    int bytes  = 0;
    *closed    = 0;
    char* data = (char*)ptr;
    char line[SOCKET_NAME_MAXLEN + 1];
    do {
        if (op == TCCL_SOCKET_RECV)
            bytes = recv(sock->fd, data + (*offset), size - (*offset), block ? 0 : MSG_DONTWAIT);
        if (op == TCCL_SOCKET_SEND)
            bytes =
                send(sock->fd, data + (*offset), size - (*offset), block ? MSG_NOSIGNAL : MSG_DONTWAIT | MSG_NOSIGNAL);
        if (op == TCCL_SOCKET_RECV && bytes == 0) {
            *closed = 1;
            return tcclSuccess;
        }
        if (bytes == -1) {
            if (errno != EINTR && errno != EWOULDBLOCK && errno != EAGAIN) {
                printf("Net : Call to recv from %s failed : %s", tcclSocketToString(&sock->addr, line),
                       strerror(errno));
                return tcclSystemError;
            } else {
                bytes = 0;
            }
        }
        (*offset) += bytes;
        if (sock->abortFlag && *sock->abortFlag != 0) {
            //   INFO(TCCL_NET, "Socket progress: abort called");
            return tcclInternalError;
        }
    } while (bytes > 0 && (*offset) < size);
    return tcclSuccess;
}

tcclResult_t tcclSocketProgress(int op, struct tcclSocket* sock, void* ptr, int size, int* offset) {
    int closed;
    TCCLCHECK(tcclSocketProgressOpt(op, sock, ptr, size, offset, 0, &closed));
    if (closed) {
        char line[SOCKET_NAME_MAXLEN + 1];
        printf("Net : Connection closed by remote peer %s", tcclSocketToString(&sock->addr, line, 0));
        return tcclSystemError;
    }
    return tcclSuccess;
}

tcclResult_t tcclSocketWait(int op, struct tcclSocket* sock, void* ptr, int size, int* offset) {
    while (*offset < size) TCCLCHECK(tcclSocketProgress(op, sock, ptr, size, offset));
    return tcclSuccess;
}

tcclResult_t tcclSocketSend(struct tcclSocket* sock, void* ptr, int size) {
    int offset = 0;
    TCCLCHECK(tcclSocketWait(TCCL_SOCKET_SEND, sock, ptr, size, &offset));
    return tcclSuccess;
}

tcclResult_t tcclSocketRecv(struct tcclSocket* sock, void* ptr, int size) {
    int offset = 0;
    TCCLCHECK(tcclSocketWait(TCCL_SOCKET_RECV, sock, ptr, size, &offset));
    return tcclSuccess;
}
