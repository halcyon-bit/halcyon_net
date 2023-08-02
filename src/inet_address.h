#pragma once

#include "platform.h"

#ifdef WINDOWS
#include <WinSock2.h>
#elif defined LINUX
#include <netinet/in.h>
#endif

#include <cstdint>
#include <string>
#include <string_view>

namespace ephemeral::net
{
    /*
     * @brief   对 struct sockaddr_in 的封装
     */
    class InetAddress
    {
    public:
        /*
         * @brief       构造函数
         * @param[in]   端口号
         */
        explicit InetAddress(uint16_t port, bool loopbackOnly = false);

        /*
         * @brief       构造函数
         * @param[in]   IP地址
         * @param[in]   端口号
         * @return
         */
        InetAddress(const std::string_view& ip, uint16_t port);

        /*
         * @brief   构造函数
         */
        InetAddress(const struct sockaddr_in& addr)
            : m_addr(addr)
        {}

        /*
         * @brief   转换为 IP:PORT 字符串
         */
        std::string toIpPort() const;

        /*
         * @brief   获取 socket 地址
         */
        const struct sockaddr_in& getSockAddr() const
        {
            return m_addr;
        }

        /*
         * @brief   设置 socket 地址
         */
        void setSockAddr(const struct sockaddr_in& addr)
        {
            m_addr = addr;
        }

    private:
        struct sockaddr_in m_addr;
    };
}