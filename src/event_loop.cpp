#include "event_loop.h"
#include "channel.h"
#include "poller.h"
#include "timer_queue.h"
#include "sockets_ops.h"

#include "log/logging.h"

#include <cassert>

using ephemeral::base::Timestamp;
using namespace ephemeral::net;

thread_local EventLoop* g_loopInThisThread = nullptr;
constexpr int g_defalutPollTimeMs = 10000;

EventLoop::EventLoop()
    : m_threadId(std::this_thread::get_id())
    , m_poller(Poller::newDefaultPoller(this))
    , m_timerQueue(new TimerQueue(this))
{
    // 创建 wakeup
#ifdef WINDOWS
    // windows 需要初始化
    WSADATA wsa_data;
    WSAStartup(0x0201, &wsa_data);

    createWakeup(m_wakeupFd, 2);
    m_wakeupChannel = std::make_unique<Channel>(this, m_wakeupFd[0]);
#elif defined LINUX
    createWakeup(&m_wakeupFd, 1);
    m_wakeupChannel = std::make_unique<Channel>(this, m_wakeupFd);
#endif

    LOG_TRACE << "created EventLoop(" << this << ") in thread " << m_threadId;
    if (g_loopInThisThread != nullptr) {
        LOG_FATAL << "Another EventLoop(" << g_loopInThisThread
            << ") exists in this thread " << m_threadId;
    }
    else {
        g_loopInThisThread = this;
    }
    // 订阅 wakeup 的可读事件
    m_wakeupChannel->setReadCallback(std::bind(&EventLoop::handleRead, this));
    m_wakeupChannel->enableRead();
}

EventLoop::~EventLoop()
{
    assert(!m_looping);
    m_wakeupChannel->disableAll();
    m_wakeupChannel->remove();
#ifdef WINDOWS
    closeWakeup(m_wakeupFd, 2);
#elif defined LINUX
    closeWakeup(&m_wakeupFd, 1);
#endif
    g_loopInThisThread = nullptr;
}

void EventLoop::loop()
{
    assert(!m_looping);
    // 确保事件循环在 IO 线程中
    assertInLoopThread();
    m_looping = true;
    m_quit = false;

    while (!m_quit) {
        m_activeChannels.clear();
        // IO multiplexing
        m_poller->poll(g_defalutPollTimeMs, &m_activeChannels);
        for (const auto& it : m_activeChannels) {
            it->handleEvent();  // 处理各文件描述符上的事件
        }

        handleFunctors();
    }

    LOG_TRACE << "EventLoop(" << this << ") stop looping";
    m_looping = false;
}

void EventLoop::quit()
{
    // FIXME：防止二次调用
    m_quit = true;
    if (!isInLoopThread()) {
        wakeup();
    }
}

void EventLoop::runInLoop(const Functor& cb)
{
    if (isInLoopThread()) {
        cb();
    }
    else {
        queueInLoop(cb);
    }
}

void EventLoop::queueInLoop(const Functor& cb)
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_functors.emplace_back(cb);
    }
    // 如果调用 queueInLoop 的线程不是 IO 线程，那么唤醒
    // 如果此时正在调用 Functor，那么也必须唤醒，否则新加的 cb
    // 就不能被及时调用了。
    // 只有在 IO 线程的事件回调中调用 queueInLoop 无需唤醒
    // 因为 Functor 的处理是在 IO 事件回调之后(详见 loop())
    if (!isInLoopThread() || m_callingFunctors) {
        wakeup();
    }
}

TimerId EventLoop::runAt(const Timestamp& time, const TimerCallback& cb)
{
    return m_timerQueue->addTimer(cb, time, 0.0);
}

TimerId EventLoop::runAfter(double delay, const TimerCallback& cb)
{
    Timestamp time(base::clock::addTime(base::clock::now(), delay));
    return runAt(time, cb);
}

TimerId EventLoop::runLoop(double interval, const TimerCallback& cb)
{
    Timestamp time(base::clock::addTime(base::clock::now(), interval));
    return m_timerQueue->addTimer(cb, time, interval);
}

void EventLoop::cancel(TimerId timerId)
{
    m_timerQueue->cancel(timerId);
}

void EventLoop::updateChannel(Channel* channel)
{
    // EventLoop 不关心 Poller 是如何管理 Channel 列表的
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    m_poller->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    m_poller->removeChannel(channel);
}

#ifdef _CHECK
bool EventLoop::hasChannel(Channel* channel)
{
    assert(channel->ownerLoop() == this);
    assertInLoopThread();
    return m_poller->hasChannel(channel);
}
#endif

EventLoop* EventLoop::getEventLoopOfCurrentThread()
{
    return g_loopInThisThread;
}

void EventLoop::abortNotInLoopThread()
{
    LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop(" << this
        << ") was created in threadId = " << m_threadId
        << ", current thread id = " << std::this_thread::get_id();
}

void EventLoop::wakeup()
{
    uint64_t one = 1;
#ifdef WINDOWS
    auto n = sockets::write(m_wakeupFd[1], &one, sizeof(one));
#elif defined LINUX
    auto n = sockets::write(m_wakeupFd, &one, sizeof(one));
#endif
    if (n != sizeof(one)) {
        LOG_ERROR << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
    }
}

void EventLoop::handleRead()
{
    uint64_t one = 1;
#ifdef WINDOWS
    size_t n = sockets::read(m_wakeupFd[0], &one, sizeof(one));
#elif defined LINUX
    size_t n = sockets::read(m_wakeupFd, &one, sizeof(one));
#endif
    if (n != sizeof(one)) {
        LOG_ERROR << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
    }
}

void EventLoop::handleFunctors()
{
    std::vector<Functor> functors;
    m_callingFunctors = true;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        functors.swap(m_functors);
    }

    for (const auto& it : functors) {
        it();
    }
    m_callingFunctors = false;
}