#pragma once

#include "base/noncopyable.h"

namespace ephemeral::net
{
    class InetAddress;
    
    /*
     * @brief   对 socket 的封装
     */
    class Socket : base::noncopyable
    {
    public:
        /*
         * @brief   构造函数
         */
        explicit Socket(int sockfd)
            : sockfd_(sockfd)
        {}

        /*
         * @brief   析构函数(关闭socket)
         */
        ~Socket();
        
        /*
         * @brief   获取文件描述符
         */
        int fd() const
        {
            return sockfd_;
        }

        /*
         * @brief   绑定 socket 地址(::bind)
         * @ps      如果地址被使用则 abort()
         */
        void bindAddress(const InetAddress& localaddr);

        /*
         * @brief   监听
         * @return  如果失败则 abort()
         */
        void listen();

        /*
         * @brief       接受一个连接(非阻塞)
         * @param[out]  客户端地址
         * @return      成功返回非负整数，失败-1
         */
        int accept(InetAddress* peeraddr);

        /*
         * @brief   启用/禁用 SO_REUSEADDR
         */
        void setReuseAddr(bool on);

        /*
         * @brief   启用/禁用 SO_REUSEPORT
         */
        void setReusePort(bool on);

        /*
         * @brief   启用/禁用 TCP_NODELAY (Nagle's algorithm)
         */
        void setTcpNoDelay(bool on);

        /*
         * @brief   启用/禁用 SO_KEEPALIVE
         */
        void setKeepAlive(bool on);

        /*
         * @brief   关闭 socket 写通道
         */
        void shutdownWrite();

    private:
        const int sockfd_;
    };
}