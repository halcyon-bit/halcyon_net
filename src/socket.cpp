#include "socket.h"

#include "inet_address.h"
#include "sockets_ops.h"

#include <cstring>

using namespace ephemeral::net;

Socket::~Socket()
{
    sockets::close(sockfd_);
}

void Socket::bindAddress(const InetAddress& addr)
{
    sockets::bindOrDie(sockfd_, addr.getSockAddr());
}

void Socket::listen()
{
    sockets::listenOrDie(sockfd_);
}

int Socket::accept(InetAddress* peeraddr)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    int connfd = sockets::accept(sockfd_, &addr);
    if (connfd >= 0) {
        peeraddr->setSockAddr(addr);
    }
    return connfd;
}

void Socket::setReuseAddr(bool on)
{
    int optval = on ? 1 : 0;
    sockets::setSockOpt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
}

void Socket::setReusePort(bool on)
{
    int optval = on ? 1 : 0;
#ifdef SO_REUSEPORT
    sockets::setSockOpt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
#endif
}

void Socket::setTcpNoDelay(bool on)
{
    int optval = on ? 1 : 0;
    sockets::setSockOpt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
}

void Socket::setKeepAlive(bool on)
{
    int optval = on ? 1 : 0;
    sockets::setSockOpt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
}

void Socket::shutdownWrite()
{
    sockets::shutdownWrite(sockfd_);
}