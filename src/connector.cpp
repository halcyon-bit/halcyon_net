#include "connector.h"
#include "channel.h"
#include "event_loop.h"
#include "sockets_ops.h"

#include "log/logging.h"

#include <cassert>
#include <algorithm>

using namespace ephemeral::net;
using std::min;

const int Connector::maxRetryDelayMs;
const int Connector::initRetryDelayMs;

Connector::Connector(EventLoop* loop, const InetAddress& serverAddr)
    : m_loop(loop)
    , m_serverAddr(serverAddr)
{}

Connector::~Connector()
{
    m_loop->cancel(m_timerId);
}

void Connector::start()
{
    m_connect = true;
    m_loop->runInLoop(std::bind(&Connector::startInLoop, shared_from_this()));
}

void Connector::startInLoop()
{
    m_loop->assertInLoopThread();
    assert(m_state == kDisconnected);
    if (m_connect) {
        connect();
    }
    else {
        LOG_DEBUG << "do not connect";
    }
}


void Connector::stop()
{
    m_connect = false;
    m_loop->queueInLoop(std::bind(&Connector::stopInLoop, shared_from_this()));
    m_loop->cancel(m_timerId);
}

void Connector::stopInLoop()
{
    m_loop->assertInLoopThread();
    if (m_state == kConnecting) {
        setState(kDisconnected);
        int sockfd = removeAndResetChannel();
        retry(sockfd);
    }
}

void Connector::connect()
{
    int sockfd = sockets::createNonblockingOrDie();
    int ret = sockets::connect(sockfd, m_serverAddr.getSockAddr());

    int savedErrno = (ret == 0) ? 0 : errno;
    switch (savedErrno) {
    case 0:
    case EINPROGRESS:
    case EINTR:
    case EISCONN:
        connecting(sockfd);
        break;

    case EAGAIN:
    case EADDRINUSE:
    case EADDRNOTAVAIL:
    case ECONNREFUSED:
    case ENETUNREACH:
        retry(sockfd);
        break;

    case EACCES:
    case EPERM:
    case EAFNOSUPPORT:
    case EALREADY:
    case EBADF:
    case EFAULT:
    case ENOTSOCK:
        LOG_SYSERR << "connect error in Connector::startInLoop " << savedErrno;
        sockets::close(sockfd);
        break;

    default:
        LOG_SYSERR << "Unexpected error in Connector::startInLoop " << savedErrno;
        sockets::close(sockfd);
        break;
    }
}

void Connector::restart()
{
    m_loop->assertInLoopThread();
    setState(kDisconnected);
    m_retryDelayMs = initRetryDelayMs;
    m_connect = true;
    startInLoop();
}


void Connector::connecting(int sockfd)
{
    setState(kConnecting);
    assert(!m_channel);
    m_channel.reset(new Channel(m_loop, sockfd));
    m_channel->setWriteCallback(std::bind(&Connector::handleWrite, shared_from_this()));
    m_channel->setErrorCallback(std::bind(&Connector::handleError, shared_from_this()));

    m_channel->enableWrite();
}

int Connector::removeAndResetChannel()
{
    m_channel->disableAll();
    m_channel->remove();
    int sockfd = m_channel->fd();
    m_loop->queueInLoop(std::bind(&Connector::resetChannel, shared_from_this()));
    return sockfd;
}

void Connector::resetChannel()
{
    m_channel.reset();
}

void Connector::handleWrite()
{
    LOG_TRACE << "Connector::handleWrite " << m_state;

    if (m_state == kConnecting) {
        int sockfd = removeAndResetChannel();
        int err = sockets::getSocketError(sockfd);
        if (err) {
            LOG_WARN << "Connector::handleWrite - SO_ERROR = "
                << err;
            retry(sockfd);
        }
        else if (sockets::isSelfConnect(sockfd)) {
            LOG_WARN << "Connector::handleWrite - Self connect";
            retry(sockfd);
        }
        else {
            setState(kConnected);
            if (m_connect) {
                m_newConnectionCallback(sockfd);
            }
            else {
                sockets::close(sockfd);
            }
        }
    }
    else {
        assert(m_state == kDisconnected);
    }
}

void Connector::handleError()
{
    LOG_ERROR << "Connector::handleError";
    assert(m_state == kConnecting);

    int sockfd = removeAndResetChannel();
    int err = sockets::getSocketError(sockfd);
    LOG_TRACE << "SO_ERROR = " << err;
    retry(sockfd);
}

void Connector::retry(int sockfd)
{
    sockets::close(sockfd);
    setState(kDisconnected);
    if (m_connect) {
        LOG_INFO << "Connector::retry - Retry connecting to "
            << m_serverAddr.toIpPort() << " in " << m_retryDelayMs << " milliseconds. ";
        m_timerId = m_loop->runAfter(m_retryDelayMs / 1000.0, std::bind(&Connector::startInLoop, this));
        m_retryDelayMs = min(m_retryDelayMs * 2, maxRetryDelayMs);
    }
    else {
        LOG_DEBUG << "do not connect";
    }
}