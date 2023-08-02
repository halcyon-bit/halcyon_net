#pragma once

#include "platform.h"

#include <cstdint>

#ifdef WINDOWS
#include <WinSock2.h>
#elif defined LINUX
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

/*
 * @brief   对一些与平台相关的函数的封装，便于上层调用
 */

namespace ephemeral::net::sockets
{
    /*
     * @brief       主机字节序和网络字节序的转换
     */
    inline uint32_t hostToNetwork32(uint32_t host32)
    {
        return htonl(host32);
    }

    inline uint16_t hostToNetwork16(uint16_t host16)
    {
        return htons(host16);
    }

    inline uint32_t networkToHost32(uint32_t net32)
    {
        return ntohl(net32);
    }

    inline uint16_t networkToHost16(uint16_t net16)
    {
        return ntohs(net16);
    }

    /*
     * @brief       IP地址转换，将二进制网络字节序的IP地址转换为点分文本的IP地址
     * @param[out]  IP:port
     * @param[in]   buf 的大小
     * @param[in]   socket 地址
     */
    void toIpPort(char* buf, size_t size, const struct sockaddr_in& addr);

    /*
     * @brief       IP地址转换，将点分文本的IP地址转换为二进制网络字节序的IP地址
     * @param[in]   IP地址
     * @param[in]   端口
     * @param[out]  socket 地址
     */
    void fromIpPort(const char* ip, uint16_t port, struct sockaddr_in* addr);

    /*
     * @brief   创建非阻塞的 socket 
     */
    int createNonblockingOrDie();

    /*
     * @brief   ::connect 连接
     */
    int connect(int sockfd, const struct sockaddr_in& addr);

    /*
     * @brief   ::bind 绑定
     */
    void bindOrDie(int sockfd, const struct sockaddr_in& addr);

    /*
     * @brief   ::listen 监听
     */
    void listenOrDie(int sockfd);

    /*
     * @brief   ::accept 接受新连接
     */
    int accept(int sockfd, struct sockaddr_in* addr);

    /*
     * @brief   向 sockfd 写信息
     */
    int write(int sockfd, const void* buf, size_t nbytes);

    /*
     * @brief   读取 sockfd 上的信息
     */
    int read(int sockfd, void* buf, size_t nbytes);

    /*
     * @brief   关闭 socket
     */
    void close(int sockfd);

    /*
     * @brief   关闭 socket 写通道，仅关闭连接，而不是套接字
     *        close 是读写全部关闭
     */
    void shutdownWrite(int sockfd);

    /*
     * @brief   设置 socket 属性
     */
    void setSockOpt(int sockfd, int level, int optname, const void* optval, int optlen);

    /*
     * @brief   获取 socket 上的错误信息
     */
    int getSocketError(int sockfd);

    /*
     * @brief   获取本机地址
     */
    struct sockaddr_in getLocalAddr(int sockfd);

    /*
     * @brief   获取远程地址
     */
    struct sockaddr_in getPeerAddr(int sockfd);

    /*
     * @brief   是否为自连接
     */
    bool isSelfConnect(int sockfd);

    /*
     * @brief   获取 socket 上可读数据的长度
     */
    int getReadBytesOfSocket(int sockfd);
}

namespace ephemeral::net
{
    /*
     * @brief   用于创建 wakeup 的文件描述符
     */
    void createWakeup(int* fd, int fdNum);

    /*
     * @brief   用于关闭 wakeup 的文件描述符
     */
    void closeWakeup(int* fd, int fdNum);
}