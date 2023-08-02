#include "sockets_ops.h"

#include "platform.h"
#include "base/types.h"
#include "log/logging.h"

#include <cassert>

#ifdef WINDOWS
#include <WS2tcpip.h>
#elif defined LINUX
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#endif

#ifdef LINUX

class IgnoreSigPipe
{
public:
    IgnoreSigPipe()
    {
        ::signal(SIGPIPE, SIG_IGN);
    }
};

IgnoreSigPipe ignore;

#endif

using SA = struct sockaddr;
namespace ephemeral::net
{
    namespace sockets
    {
        const SA* sockaddr_cast(const struct sockaddr_in* addr)
        {
            return static_cast<const SA*>(base::implicit_cast<const void*>(addr));
        }

        SA* sockaddr_cast(struct sockaddr_in* addr)
        {
            return static_cast<SA*>(base::implicit_cast<void*>(addr));
        }

        void setNonBlockAndCloseOnExec(int sockfd)
        {
#ifdef WINDOWS
            unsigned long ul = 1;
            int ret = ioctlsocket(sockfd, FIONBIO, (unsigned long*)&ul);
#elif defined LINUX
            int flags = ::fcntl(sockfd, F_GETFL, 0);
            flags |= O_NONBLOCK;
            int ret = ::fcntl(sockfd, F_SETFL, flags);

            flags = ::fcntl(sockfd, F_GETFL, 0);
            flags |= FD_CLOEXEC;
            ret = ::fcntl(sockfd, F_SETFD, flags);
#endif
        }

        void toIpPort(char* buf, size_t size, const struct sockaddr_in& addr)
        {
            char host[INET_ADDRSTRLEN] = "INVALID";
            ::inet_ntop(AF_INET, &addr.sin_addr, host, sizeof(host));
            uint16_t port = networkToHost16(addr.sin_port);
            snprintf(buf, size, "%s:%u", host, port);
        }

        void fromIpPort(const char* ip, uint16_t port, struct sockaddr_in* addr)
        {
            addr->sin_family = AF_INET;
            addr->sin_port = hostToNetwork16(port);
            if (::inet_pton(AF_INET, ip, &addr->sin_addr) <= 0) {
                LOG_SYSERR << "sockets::fromHostPort";
            }
        }

        int createNonblockingOrDie()
        {
#ifdef WINDOWS
            int sockfd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sockfd < 0) {
                LOG_SYSFATAL << "sockets::createNonblockingOrDie";
            }
            setNonBlockAndCloseOnExec(sockfd);
            return sockfd;
#elif defined LINUX
            int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
            if (sockfd < 0) {
                LOG_SYSFATAL << "sockets::createNonblockingOrDie";
            }
            return sockfd;
#endif
        }

        int connect(int sockfd, const struct sockaddr_in& addr)
        {
            return ::connect(sockfd, sockaddr_cast(&addr), sizeof(addr));
        }

        void bindOrDie(int sockfd, const struct sockaddr_in& addr)
        {
            int ret = ::bind(sockfd, sockaddr_cast(&addr), sizeof(addr));
            if (ret < 0) {
                LOG_SYSFATAL << "sockets::bindOrDie";
            }
        }

        void listenOrDie(int sockfd)
        {
            int ret = ::listen(sockfd, SOMAXCONN);
            if (ret < 0) {
                LOG_SYSFATAL << "sockets::listenOrDie";
            }
        }

        int accept(int sockfd, struct sockaddr_in* addr)
        {
            socklen_t addrlen = sizeof(*addr);
            int connfd = ::accept(sockfd, sockaddr_cast(addr), &addrlen);
            setNonBlockAndCloseOnExec(connfd);
            if (connfd < 0) {
#ifdef WINDOWS
#elif defined LINUX
                int savedErrno = errno;
                LOG_SYSERR << "Socket::accept";
                switch (savedErrno) {
                    case EAGAIN:
                    case ECONNABORTED:
                    case EINTR:
                    case EPROTO: // ???
                    case EPERM:
                    case EMFILE: // per-process lmit of open file desctiptor ???
                    // expected errors
                        errno = savedErrno;
                        break;
                    case EBADF:
                    case EFAULT:
                    case EINVAL:
                    case ENFILE:
                    case ENOBUFS:
                    case ENOMEM:
                    case ENOTSOCK:
                    case EOPNOTSUPP:
                    // unexpected errors
                        LOG_FATAL << "unexpected error of ::accept " << savedErrno;
                        break;
                    default:
                        LOG_FATAL << "unknown error of ::accept " << savedErrno;
                        break;
                }
#endif
            }
            return connfd;
        }

        int write(int fd, const void* buf, size_t nbytes)
        {
#ifdef WINDOWS
            auto n = ::send(fd, static_cast<const char*>(buf), static_cast<int>(nbytes), 0);
#elif defined LINUX
            auto n = ::write(fd, buf, nbytes);
#endif
            return n;
        }

        int read(int fd, void* buf, size_t nbytes)
        {
#ifdef WINDOWS
            auto n = ::recv(fd, static_cast<char*>(buf), static_cast<int>(nbytes), 0);
#elif defined LINUX
            auto n = ::read(fd, &buf, nbytes);
#endif
            return n;
        }

        void close(int sockfd)
        {
#ifdef WINDOWS
            if (::closesocket(sockfd)) {
#elif defined LINUX
            if (::close(sockfd) < 0) {
#endif
                LOG_SYSERR << "sockets::close";
            }
        }

        void shutdownWrite(int sockfd)
        {
            // int shutdown(int sock, int howto)
            // sock 为需要断开的套接字，howto 为断开方式
            // howto 在 Linux 下有以下取值:
            //    SHUT_RD: 断开输入流。套接字无法接收数据(即使
            //       输入缓冲区收到数据也被抹去)，无法调用输入相关函数。
            //    SHUT_WR: 断开输出流。套接字无法发送数据，但如
            //       果输出缓冲区中还有未传输的数据，则将传递到目标主机。
            //    SHUT_RDWR: 同时断开 I/O 流。相当于分两次调用 shutdown()，
            //       其中一次以 SHUT_RD 为参数，另一次以 SHUT_WR 为参数。
            // howto 在 Windows 下有以下取值:
            //    SD_RECEIVE: 关闭接收操作，也就是断开输入流。
            //    SD_SEND: 关闭发送操作，也就是断开输出流。
            //    SD_BOTH: 同时关闭接收和发送操作。
#ifdef WINDOWS
            if (::shutdown(sockfd, SD_SEND)) {
#elif defined LINUX
            if (::shutdown(sockfd, SHUT_WR) < 0) {
#endif
                LOG_SYSERR << "sockets::shutdownWrite";
            }
            // 确切地说，close() 用来关闭套接字，将套接字描述符从内存清除，之后
            // 再也不能使用该套接字，与C语言中的 fclose() 类似。应用程序关闭套接
            // 字后，与该套接字相关的连接和缓存也失去了意义，TCP协议会自动触发关
            // 闭连接的操作。

            // shutdown() 用来关闭连接，而不是套接字，不管调用多少次 shutdown()，
            // 套接字依然存在，直到调用 close() 将套接字从内存清除。

            // 调用 close() 关闭套接字时，或调用 shutdown() 关闭输出流时，都会向
            // 对方发送 FIN 包。FIN 包表示数据传输完毕，计算机收到 FIN 包就知道不
            // 会再有数据传送过来了。

            // 默认情况下，close() 会立即向网络中发送FIN包，不管输出缓冲区中是否还
            // 有数据，而shutdown() 会等输出缓冲区中的数据传输完毕再发送FIN包。也就
            // 意味着，调用 close() 将丢失输出缓冲区中的数据，而调用 shutdown() 不会。
        }

        void setSockOpt(int sockfd, int level, int optname, const void* optval, int optlen)
        {
#ifdef WINDOWS
            ::setsockopt(sockfd, level, optname, static_cast<const char*>(optval), optlen);
#elif defined LINUX
            ::setsockopt(sockfd, level, optname, optval, optlen);
#endif
        }

        int getSocketError(int sockfd)
        {
            int optval;
            socklen_t optlen = sizeof(optval);

            if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char*)(&optval), &optlen) < 0) {
                return errno;
            }
            else {
                return optval;
            }
        }

        struct sockaddr_in getLocalAddr(int sockfd)
        {
            struct sockaddr_in localaddr;
            memset(&localaddr, 0, sizeof(localaddr));
            socklen_t addrlen = sizeof(localaddr);
            if (::getsockname(sockfd, sockaddr_cast(&localaddr), &addrlen) < 0) {
                LOG_SYSERR << "sockets::getLocalAddr";
            }
            return localaddr;
        }

        struct sockaddr_in getPeerAddr(int sockfd)
        {
            struct sockaddr_in peeraddr;
            memset(&peeraddr, 0, sizeof(peeraddr));
            socklen_t addrlen = sizeof(peeraddr);
            if (::getpeername(sockfd, sockaddr_cast(&peeraddr), &addrlen) < 0) {
                LOG_SYSERR << "sockets::getPeerAddr";
            }
            return peeraddr;
        }

        bool isSelfConnect(int sockfd)
        {
            struct sockaddr_in localaddr = getLocalAddr(sockfd);
            struct sockaddr_in peeraddr = getPeerAddr(sockfd);
            return localaddr.sin_port == peeraddr.sin_port
                && localaddr.sin_addr.s_addr == peeraddr.sin_addr.s_addr;
        }

        int getReadBytesOfSocket(int sockfd)
        {
            unsigned long bytes = 0;
#ifdef WINDOWS
            ::ioctlsocket(sockfd, FIONREAD, &bytes);
#elif defined LINUX
            ::ioctl(sockfd, FIONREAD, &bytes);
#endif
            return bytes;
        }
    }
}


namespace ephemeral::net
{
#ifdef WINDOWS
    // 创建TCP服务
    static int createTcpServer(const char* ip, int port, int listenNum)
    {
        int sockfd = ::socket(PF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            return -1;
        }
        struct sockaddr_in svrAddr;
        memset(&svrAddr, 0, sizeof(svrAddr));
        svrAddr.sin_family = AF_INET;
        sockets::fromIpPort(ip, port, &svrAddr);

        if (::bind(sockfd, sockets::sockaddr_cast(&svrAddr), sizeof(struct sockaddr)) < 0) {
            return -1;
        }

        if (::listen(sockfd, listenNum) < 0) {
            return -1;
        }
        return sockfd;
    }

    void createWakeup(int* fd, int fdNum)
    {
        assert(nullptr != fd && 2 == fdNum);
        int svrfd = createTcpServer("127.0.0.1", 0, 1);
        if (svrfd < 0) {
            LOG_ERROR << "falied in createWakeup";
            abort();
        }

        fd[0] = ::socket(AF_INET, SOCK_STREAM, 0);
        u_long nonBlk = 1;
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));

        socklen_t addrlen = sizeof(addr);
        if (::getsockname(svrfd, sockets::sockaddr_cast(&addr), &addrlen))
            goto fail;

        if (::connect(fd[0], sockets::sockaddr_cast(&addr), addrlen))
            goto fail;

        if ((fd[1] = ::accept(svrfd, 0, 0)) < 0)
            goto fail;

        (void)::ioctlsocket(fd[0], FIONBIO, &nonBlk);
        (void)::ioctlsocket(fd[1], FIONBIO, &nonBlk);

        ::closesocket(svrfd);
        return;

    fail:
        ::closesocket(svrfd);
        if (fd[0] != -1) {
            ::closesocket(fd[0]);
        }
        if (fd[1] != -1) {
            ::closesocket(fd[1]);
        }
        abort();
    }

    void closeWakeup(int* fd, int fdNum)
    {
        assert(nullptr != fd && 2 == fdNum);
        assert(fd[0] != -1);
        ::closesocket(fd[0]);
        assert(fd[1] != -1);
        ::closesocket(fd[1]);
        //fd[0] = -1;
        //fd[1] = -1;
    }
#elif defined LINUX
    static int createEventfd()
    {
        int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (evtfd < 0) {
            LOG_SYSERR << "falied in createEventfd";
            return -1;
        }
        return evtfd;
    }

    void createWakeup(int* fd, int fdNum)
    {
        assert(nullptr != fd && 1 == fdNum);
        *fd = createEventfd();
        if (*fd < 0) {
            abort();
        }
    }

    void closeWakeup(int* fd, int fdNum)
    {
        ::close(*fd);
    }
#endif

}
