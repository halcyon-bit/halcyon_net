#include "tcp_server.h"
#include "acceptor.h"
#include "event_loop.h"
#include "sockets_ops.h"
#include "event_loop_thread_pool.h"

#include "log/logging.h"

#include <cassert>

using namespace ephemeral::net;

TcpServer::TcpServer(EventLoop* loop, const InetAddress& listenAddr, const std::string_view& name, bool reuseport)
    : m_loop(CHECK_NOTNULL(loop))
    , m_name(name)
    , m_acceptor(new Acceptor(loop, listenAddr, reuseport))
    , m_threadPool(new EventLoopThreadPool(loop))
{
    // 设置新连接的处理函数
    m_acceptor->setNewConnectionCallback(std::bind(&TcpServer::handleConnection, this,
        std::placeholders::_1, std::placeholders::_2));
}

TcpServer::~TcpServer()
{
    m_loop->assertInLoopThread();
    for (auto& item : m_connections) {
        TcpConnectionSPtr conn(item.second);
        item.second.reset();
        // 在 TcpConnection 所在的 EventLoop 处理
        conn->getLoop()->runInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
    }
}

void TcpServer::setThreadNum(int numThreads)
{
    assert(0 <= numThreads);
    m_threadPool->setThreadNum(numThreads);
}

void TcpServer::start()
{
    if (!m_started) {
        m_started = true;
        m_threadPool->start();
        assert(!m_acceptor->listenning());

        // 监听 socket 的 EventLoop 是 m_loop
        m_loop->runInLoop(std::bind(&Acceptor::listen, m_acceptor.get()));
    }
}

void TcpServer::handleConnection(int sockfd, const InetAddress& peerAddr)
{
    m_loop->assertInLoopThread();
    // 新 TcpConnection 的名称
    char buf[32];
    snprintf(buf, sizeof(buf), "#%d", m_nextConnId);
    ++m_nextConnId;
    std::string connName = m_name + buf;

    LOG_INFO << "new connection[" << connName << "] from " << peerAddr.toIpPort();
    InetAddress localAddr(sockets::getLocalAddr(sockfd));

    // 为 TcpConnection 分配一个 EventLoop
    EventLoop* loop = m_threadPool->getNextLoop();
    TcpConnectionSPtr conn = std::make_shared<TcpConnection>(loop, connName, sockfd, localAddr, peerAddr);
    m_connections[connName] = conn;

    // 设置连接回调(用于通知用户)
    conn->setConnectionCallback(m_connectionCallback);
    // 设置读事件回调(用于通知用户)
    conn->setMessageCallback(m_messageCallback);
    // 设置写完成回调(用于通知用户)
    conn->setWriteCompleteCallback(m_writeCompleteCallback);
    // 设置断线回调(用于通知内部，进行清理操作)
    conn->setCloseCallback(std::bind(&TcpServer::handleDisConnection, this, std::placeholders::_1));
    // 设置好相应事件，需要在对应的 IO 线程中运行
    loop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::handleDisConnection(const TcpConnectionSPtr& conn)
{
    // 由 TcpConnection 调用，EventLoop 不一定是 m_loop，所以
    // 需要在 m_loop 中调用，确保线程安全
    m_loop->runInLoop(std::bind(&TcpServer::handleDisConnectionInLoop, this, conn));
}

void TcpServer::handleDisConnectionInLoop(const TcpConnectionSPtr& conn)
{
    m_loop->assertInLoopThread();
    LOG_INFO << "remove connection[" << conn->name() << "]";
    // 移除 TcpConnectionSPtr
    size_t n = m_connections.erase(conn->name());
    assert(n == 1); (void)n;
    // 这时如果用户不持有 TcpConnection，conn 的引用计数为 1
    // 使用 bind 延长 conn 的生命，直到 connectDestroyed 调用完成
    EventLoop* loop = conn->getLoop();
    // 反注册 TcpConnection 中的事件
    loop->queueInLoop(std::bind(&TcpConnection::connectDestroyed, conn));
}