#include "tcp_client.h"
#include "connector.h"
#include "event_loop.h"
#include "sockets_ops.h"

#include "log/logging.h"

namespace ephemeral::net::detail
{
    void removeConnection(EventLoop* loop, const TcpConnectionSPtr& conn)
    {
        loop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
    }

    void removeConnector(const ConnectorSPtr& connector)
    {

    }
}

using namespace ephemeral::net;

TcpClient::TcpClient(EventLoop* loop, const InetAddress& serverAddr, const std::string_view& name)
    : m_loop(CHECK_NOTNULL(loop))
    , m_connector(new Connector(loop, serverAddr))
    , m_name(name)
{
    m_connector->setNewConnectionCallback(std::bind(&TcpClient::handleConnection, this, std::placeholders::_1));
}

TcpClient::~TcpClient()
{
    TcpConnectionSPtr conn;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        conn = m_connection;
    }
    if (conn) {
        CloseCallback cb = std::bind(&detail::removeConnection, m_loop, std::placeholders::_1);
        m_loop->runInLoop(std::bind(&TcpConnection::setCloseCallback, conn, cb));
    }
    else {
        m_connector->stop();
        m_loop->runAfter(1, std::bind(&detail::removeConnector, m_connector));
    }
}

void TcpClient::connect()
{
    m_connect = true;
    m_connector->start();
}

void TcpClient::disconnect()
{
    m_connect = false;

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_connection) {
            m_connection->shutdown();
        }
    }
}

void TcpClient::stop()
{
    m_connect = false;
    m_connector->stop();
}

void TcpClient::handleConnection(int sockfd)
{
    m_loop->assertInLoopThread();

    InetAddress peerAddr(sockets::getPeerAddr(sockfd));
    char buf[32];
    snprintf(buf, sizeof(buf), ":%s#%d", peerAddr.toIpPort().c_str(), m_nextConnId);
    ++m_nextConnId;
    std::string connName{ buf };

    InetAddress localAddr(sockets::getLocalAddr(sockfd));
    TcpConnectionSPtr conn = std::make_shared<TcpConnection>(m_loop, connName, sockfd, localAddr, peerAddr);

    conn->setConnectionCallback(m_connectionCallback);
    conn->setMessageCallback(m_messageCallback);
    conn->setWriteCompleteCallback(m_writeCompleteCallback);
    conn->setCloseCallback(std::bind(&TcpClient::handleDisConnection, this, std::placeholders::_1));
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_connection = conn;
    }
    conn->connectEstablished();
}

void TcpClient::handleDisConnection(const TcpConnectionSPtr& conn)
{
    m_loop->assertInLoopThread();
    assert(m_loop == conn->getLoop());

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        assert(m_connection == conn);
        m_connection.reset();
    }

    m_loop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
    if (m_retry && m_connect) {
        m_connector->restart();
    }
}