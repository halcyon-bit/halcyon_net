#include "acceptor.h"
#include "event_loop.h"
#include "inet_address.h"
#include "sockets_ops.h"

#include "log/logging.h"

using namespace ephemeral::net;

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport)
    : m_loop(loop)
    , m_acceptSocket(sockets::createNonblockingOrDie())
    , m_acceptChannel(loop, m_acceptSocket.fd())
{
    // 创建监听 socket
    m_acceptSocket.setReuseAddr(false);
    m_acceptSocket.setReusePort(reuseport);
    m_acceptSocket.bindAddress(listenAddr);  // 绑定地址
    // 设置读事件回调(即有新连接的到来)
    m_acceptChannel.setReadCallback(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
    // 取消关注的事件，并从 Poller 中移除 Channel
    m_acceptChannel.disableAll();
    m_acceptChannel.remove();
}

void Acceptor::listen()
{
    // 开启监听
    m_loop->assertInLoopThread();
    m_listen = true;
    m_acceptSocket.listen();
    // 订阅读事件，仅需关注监听 socket 的读事件
    m_acceptChannel.enableRead();
}

void Acceptor::handleRead()
{
    // 新的连接到来，处理新连接
    m_loop->assertInLoopThread();
    InetAddress peerAddr(0);  // 客户端地址
    // client socket
    int connfd = m_acceptSocket.accept(&peerAddr);
    // FIXME：文件描述符耗尽的情况
    if (connfd >= 0) {
        // 调用处理函数
        if (m_newConnectionCallback) {
            m_newConnectionCallback(connfd, peerAddr);
        }
        else {
            sockets::close(connfd);
        }
    }
    else {
        LOG_ERROR << "accept error in Acceptor::handleRead";
    }
}