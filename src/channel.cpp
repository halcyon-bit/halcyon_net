#include "channel.h"
#include "event_loop.h"

#include "log/logging.h"

#include <cassert>

using namespace ephemeral::net;

Channel::Channel(EventLoop* loop, int fd)
    : m_loop(loop)
    , m_fd(fd)
{}

Channel::~Channel()
{
    assert(!m_eventHandling);
#ifdef _CHECK
    assert(!m_addedToLoop);
    if (m_loop->isInLoopThread()) {
        assert(!m_loop->hasChannel(this));
    }
#endif
}

void Channel::handleEvent()
{
    // chanel 总是另一个类的成员变量，e.g Acceptor，TcpConnection
    // 由于 TcpConnection 是通过 std::shared_ptr 管理，所以可能存在
    // 在处理事件时，TcpConnection 对象被释放，使用使用 std::weak_ptr 
    // 来判断其有效性。
    std::shared_ptr<void> guard;
    if (m_isTie) {
        guard = m_tie.lock();
        if (guard) {
            handleEventWithGuard();
        }
    }
    else {
        handleEventWithGuard();
    }
}

void Channel::tie(const std::shared_ptr<void>& obj)
{
    m_tie = obj;
    m_isTie = true;
}

void Channel::remove()
{
    assert(isNoneEvent());
#ifdef _CHECK
    m_addedToLoop = false;
#endif
    m_loop->removeChannel(this);
}

void Channel::update()
{
    // 调用 EventLoop::updateChannel()，后者会转而调用
    // Poller::updateChannel()
#ifdef _CHECK
    m_addedToLoop = true;
#endif
    m_loop->updateChannel(this);
}

void Channel::handleEventWithGuard()
{
    m_eventHandling = true;
    // 读事件
    if (m_revents & EVENT_TYPE_READ) {
        if (m_readCallback) {
            m_readCallback();
        }
    }
    // 写事件
    if (m_revents & EVENT_TYPE_WRITE) {
        if (m_writeCallback) {
            m_writeCallback();
        }
    }
    // 错误事件
    if (m_revents & EVENT_TYPE_ERROR) {
        if (m_errorCallback) {
            m_errorCallback();
        }
    }
    // 关闭事件
    if (m_revents & EVENT_TYPE_CLOSE) {
        if (m_closeCallback) {
            m_closeCallback();
        }
    }
    m_eventHandling = false;
}