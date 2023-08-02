#include "inet_address.h"
#include "sockets_ops.h"

#include <cstring>

using namespace ephemeral::net;

static_assert(sizeof(InetAddress) == sizeof(struct sockaddr_in), "InetAddress is same size as sockaddr_in");

InetAddress::InetAddress(uint16_t port, bool loopbackOnly)
{
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin_family = AF_INET;
    m_addr.sin_addr.s_addr = sockets::hostToNetwork32(loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
    m_addr.sin_port = sockets::hostToNetwork16(port);
}

InetAddress::InetAddress(const std::string_view& ip, uint16_t port)
{
    memset(&m_addr, 0, sizeof(m_addr));
    sockets::fromIpPort(ip.data(), port, &m_addr);
}

std::string InetAddress::toIpPort() const
{
    char buf[32]{0};
    sockets::toIpPort(buf, sizeof(buf), m_addr);
    return buf;
}