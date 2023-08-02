#include "tcp_connection.h"
#include "socket.h"
#include "channel.h"
#include "event_loop.h"
#include "sockets_ops.h"

#include "log/logging.h"
#include "callback/weak_callback.h"

#include <cassert>

using namespace ephemeral::net;

void ephemeral::net::defaultConnectionCallback(const TcpConnectionSPtr& conn)
{

}

void ephemeral::net::defaultMessageCallback(const TcpConnectionSPtr& conn, Buffer* buffer)
{
    buffer->reset();
}

TcpConnection::TcpConnection(EventLoop* loop, const std::string_view& name, int sockfd, const InetAddress& localAddr, const InetAddress& peerAddr)
    : m_loop(CHECK_NOTNULL(loop))
    , m_name(name)
    , m_socket(new Socket(sockfd))
    , m_channel(new Channel(loop, sockfd))
    , m_localAddr(localAddr)
    , m_peerAddr(peerAddr)
{
    LOG_DEBUG << "TcpConnection: " << m_name;
    // 注册相关事件回调
    m_channel->setReadCallback(std::bind(&TcpConnection::handleRead, this));
    m_channel->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    m_channel->setErrorCallback(std::bind(&TcpConnection::handleError, this));
    m_channel->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
    m_socket->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
    LOG_DEBUG << "~TcpConnection: " << m_name;
    assert(m_state == kDisconnected);
}

void TcpConnection::send(const std::string_view& message)
{
    send(message.data(), message.size());
}

void TcpConnection::send(const void* message, int len)
{
    if (m_state == kConnected) {
        if (m_loop->isInLoopThread()) {
            sendInLoop(message, len);
        }
        else {
            std::string msg(static_cast<const char*>(message), len);
            void (TcpConnection::*fp)(const std::string_view& message) = &TcpConnection::sendInLoop;
            m_loop->runInLoop(std::bind(fp, shared_from_this(), msg));
        }
    }
}

void TcpConnection::send(Buffer* message)
{
    if (m_state == kConnected) {
        if (m_loop->isInLoopThread()) {
            sendInLoop(message->peek(), message->readableBytes());
            message->reset();
        }
        else {
            void (TcpConnection::*fp)(const std::string_view& message) = &TcpConnection::sendInLoop;
            m_loop->runInLoop(std::bind(fp, shared_from_this(), message->retrieveAsString()));
        }
    }
}

void TcpConnection::shutdown()
{
    if (m_state == kConnected) {
        // 处理关闭
        setState(kDisconnecting);
        m_loop->runInLoop(std::bind(&TcpConnection::shutdownInLoop, shared_from_this()));
    }
}

void TcpConnection::forceClose()
{
    if (m_state == kConnected || m_state == kDisconnecting) {
        setState(kDisconnecting);
        m_loop->queueInLoop(std::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
    }
}

void TcpConnection::forceCloseWithDelay(double seconds)
{
    if (m_state == kConnected || m_state == kDisconnecting) {
        setState(kDisconnecting);
        m_loop->runAfter(seconds, base::makeWeakCallback(shared_from_this(), &TcpConnection::forceClose));
    }
}

void TcpConnection::setTcpNoDelay(bool on)
{
    m_socket->setTcpNoDelay(on);
}

void TcpConnection::startRead()
{
    m_loop->runInLoop(std::bind(&TcpConnection::startReadInLoop, this));
}

void TcpConnection::stopRead()
{
    m_loop->runInLoop(std::bind(&TcpConnection::stopReadInLoop, this));
}

void TcpConnection::connectEstablished()
{
    // 新连接的到来
    m_loop->assertInLoopThread();
    assert(m_state == kConnecting);
    setState(kConnected);
    m_channel->tie(shared_from_this());
    m_channel->enableRead();
    // 通知用户，连接已建立
    m_connectionCallback(shared_from_this());
}

void TcpConnection::connectDestroyed()
{
    // 两种调用，1 服务器析构时，
    // 2 客户端连接断开时，已经进行了部分处理，状态已为 kConnected
    m_loop->assertInLoopThread();
    if (m_state == kConnected) {
        setState(kDisconnected);
        // 取消事件订阅
        m_channel->disableAll();
        // 通知用户，连接已断开
        m_connectionCallback(shared_from_this());
    }
    m_channel->remove();
}

void TcpConnection::handleRead()
{
    m_loop->assertInLoopThread();
    // 读取数据，并向用户传递
    int savedErrno = 0;
    int n = m_inputBuffer.readFd(m_channel->fd(), &savedErrno);
    if (n > 0) {
        m_messageCallback(shared_from_this(), &m_inputBuffer);
    }
    else if (n == 0) {
        // 0：表示连接已关闭，被动关闭
        handleClose();
    }
    else {
        LOG_SYSERR << "TcpConnection::handleRead";
        handleError();
    }
}

void TcpConnection::handleWrite()
{
    m_loop->assertInLoopThread();
    // 是否订阅写事件
    if (m_channel->isWriting()) {
        int n = sockets::write(m_channel->fd(), m_outputBuffer.peek(), m_outputBuffer.readableBytes());
        if (n > 0) {
            m_outputBuffer.retrieve(n);
            if (m_outputBuffer.readableBytes() == 0) {
                // 仅在需要时关注写事件，即还有数据没有发送
                m_channel->disableWrite();
                if (m_writeCompleteCallback) {
                    m_loop->queueInLoop(std::bind(m_writeCompleteCallback, shared_from_this()));
                }
                if (m_state == kDisconnecting) {
                    // 若当前处于连接关闭状态，关闭连接
                    shutdownInLoop();
                }
            }
        }
        else {
            LOG_SYSERR << "TcpConnection::handleWrite";
        }
    }
    else {
        LOG_TRACE << "Connection is down, no more writing";
    }
}

void TcpConnection::handleError()
{
    int err = sockets::getSocketError(m_channel->fd());
    LOG_ERROR << "TcpConnection::handleError[" << m_name
        << "] - SO_ERROR = " << err;
}

void TcpConnection::handleClose()
{
    m_loop->assertInLoopThread();
    LOG_TRACE << "TcpConnection::handleClose";
    assert(m_state == kConnected || m_state == kDisconnecting);
    setState(kDisconnected);
    // 取消事件的关注
    m_channel->disableAll();

    // 通知用户连接关闭
    m_connectionCallback(shared_from_this());
    // 内部通知，清理数据，需放在最后调用
    m_closeCallback(shared_from_this());
}

void TcpConnection::sendInLoop(const void* data, size_t len)
{
    m_loop->assertInLoopThread();
    int nwrote = 0;
    if (m_state == kDisconnected) {
        LOG_WARN << "disconnected, give up writing";
        return;
    }
    size_t remaining = len;  // 未发送的数据长度

    // 当没有上次遗留的数据，则尝试直接发送，否则放入 m_outpuBuffer
    if (!m_channel->isWriting() && m_outputBuffer.readableBytes() == 0) {
        nwrote = sockets::write(m_channel->fd(), data, len);
        if (nwrote >= 0) {
            remaining = len - nwrote;
            if (remaining == 0 && m_writeCompleteCallback) {
                m_loop->queueInLoop(std::bind(m_writeCompleteCallback, shared_from_this()));
            }
        }
        else {
            nwrote = 0;
            if (errno != EWOULDBLOCK) {
                LOG_SYSERR << "TcpConnection::sendInLoop";
            }
        }
    }

    assert(remaining <= len);
    if (remaining > 0) {
        size_t oldLen = m_outputBuffer.readableBytes();
        if (oldLen + remaining >= m_highWaterMark
            && oldLen < m_highWaterMark && m_highWaterMarkCallback) {
            m_loop->queueInLoop(std::bind(m_highWaterMarkCallback, shared_from_this(), oldLen + remaining));
        }

        // 有没有完全发送出去的数据，等待下次 socket 可写时发送
        m_outputBuffer.append(static_cast<const char*>(data) + nwrote, remaining);
        if (!m_channel->isWriting()) {
            // 订阅 socket  的可写事件，用于继续发送遗留数据
            m_channel->enableWrite();
        }
    }
}

void TcpConnection::sendInLoop(const std::string_view& message)
{
    sendInLoop(message.data(), message.size());
}

void TcpConnection::shutdownInLoop()
{
    m_loop->assertInLoopThread();
    if (!m_channel->isWriting()) {
        // 若数据全部发送出去后，直接关闭
        m_socket->shutdownWrite();
    }
    // else 否则等待数据发送完成后，再关闭连接，在 handleWrite 中关闭
    // 根据连接状态决定(m_state)
}

void TcpConnection::forceCloseInLoop()
{
    m_loop->assertInLoopThread();
    if (m_state == kConnected || m_state == kDisconnecting) {
        handleClose();
    }
}

void TcpConnection::startReadInLoop()
{
    m_loop->assertInLoopThread();
    if (!m_reading || !m_channel->isReading()) {
        m_channel->enableRead();
        m_reading = true;
    }
}

void TcpConnection::stopReadInLoop()
{
    m_loop->assertInLoopThread();
    if (m_reading || m_channel->isReading()) {
        m_channel->disableRead();
        m_reading = false;
    }
}