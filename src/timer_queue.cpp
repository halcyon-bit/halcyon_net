#include "timer_queue.h"
#include "event_loop.h"
#include "timer.h"
#include "timer_id.h"

#include "log/logging.h"

#include <cassert>
#include <cstdint>
#define WINDOWS
using namespace ephemeral::base;

#ifdef LINUX
#include <unistd.h>
#include <sys/timerfd.h>

namespace ephemeral::net::detail
{
    /*
     * @brief   创建 timerfd
     */
    int createTimerfd()
    {
        // 创建一个新的 timer 对象，返回一个文件描述符对应于这个 timer
        // clockid 参数：指定时钟，用于标志 timer 的进度，它必须是下面的一个：
        //      CLOCK_REALTIME：一个可设置的系统范围的实时时钟
        //          相对时间，从1970.1.1到目前的时间。更改系统时间会更改获取的值。它以系统时间为坐标
        //      CLOCK_MONOTONIC：一个不可设置的单调递增的时钟，从过去未指定的某个时间点开始测量时间，时间在系统启动后不会再改变
        //          以绝对时间为准，获取的时间为系统重启到现在的时间，更改系统时间对它没有影响
        // flags 参数：
        //      TFD_NONBLOCK：置位 O_NONBLOCK 参数
        //      TFD_CLOEXEC：置位 O_CLOEXEC 参数
        int timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (timerfd < 0) {
            LOG_SYSFATAL << "failed in timerfd_create";
        }
        return timerfd;
    }

    /*
     * @brief       计算时间差值(when -> now)
     * @param[in]   起始时间
     */
    struct timespec howMuchTimeFromNow(Timestamp when)
    {
        int64_t microseconds = clock::microSecondsSinceEpoch(when)
            - clock::microSecondsSinceEpoch(clock::now());

        if (microseconds < 100) {
            microseconds = 100;
        }
        // 表示时间
        // struct timespec {
        //      time_t tv_sec;  // 秒
        //      long tv_nsec;  // 毫秒
        // };
        struct timespec ts;
        ts.tv_sec = static_cast<time_t>(microseconds / clock::microSecondsPerSecond);
        ts.tv_nsec = static_cast<time_t>((microseconds % clock::microSecondsPerSecond) * 1000);
        return ts;
    }

    void readTimerfd(int timerfd, base::Timestamp now)
    {
        uint64_t howmany;
        int n = ::read(timerfd, &howmany, sizeof(howmany));
        LOG_TRACE << "TimerQueue::handleRead " << howmany << " at " << clock::toString(now);
        if (n != sizeof(howmany)) {
            LOG_ERROR << "TimerQueue::handleRead reads " << n << " bytes instead of 8";
        }
    }

    /*
     * @brief       调整定时器(timerfd)的超时时间
     * @param[in]   timerfd
     * @param[in]   超时时间(绝对时间)
     */
    void resetTimerfd(int timerfd, base::Timestamp expiration)
    {
        // itimerspec 用于设置定时器的工作方式
        // struct itimerspec {
        //      struct timespec it_interval;  // 表示之后的超时时间,即每隔多长时间超时
        //      struct timespec it_value;  // 表示定时器第一次超时时间
        // };
        struct itimerspec newValue;
        struct itimerspec oldValue;
        memset(&newValue, 0, sizeof(newValue));
        memset(&oldValue, 0, sizeof(oldValue));

        newValue.it_value = howMuchTimeFromNow(expiration);  // 设置超时时间(相对时间)
        // int timerfd_settime(int fd, int flags, const struct itimerspec* new_value, struct itimerspec* old_value);
        // new_value：指定新的超时时间，设定 new_value.it_value 非零则启动定时器，否则关闭定时器，
        //      如果 new_value.it_interval 为 0，则定时器只定时一次，即初始那次，否则之后每隔设定时间超时一次
        // old_value：不为 null，则返回定时器这次设置之前的超时时间
        // flags：1 代表设置的是绝对时间；为 0 代表相对时间
        int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
        if (ret) {
            LOG_SYSERR << "timerfd_settime";
        }
    }
}
#endif

using namespace ephemeral::net;

#ifdef LINUX
TimerQueue::TimerQueue(EventLoop* loop)
    : m_loop(loop)
    , m_timerfd(detail::createTimerfd())
    , m_timerfdChannel(loop, m_timerfd)
{
    // 设置并订阅读事件
    m_timerfdChannel.setReadCallback(std::bind(&TimerQueue::handleRead, this));
    m_timerfdChannel.enableRead();
}

TimerQueue::~TimerQueue()
{
    ::close(m_timerfd);
    for (auto& it : m_timers) {
        delete it.second;
    }
}

TimerId TimerQueue::addTimer(const TimerCallback& cb, base::Timestamp when, double interval)
{
    Timer* timer = new Timer(cb, when, interval);
    // 线程安全，无需加锁保护
    m_loop->runInLoop(std::bind(&TimerQueue::addTimerInLoop, this, timer));

    return TimerId(timer, timer->sequence());
}

void TimerQueue::cancel(TimerId timerId)
{
    m_loop->runInLoop(std::bind(&TimerQueue::cancelInLoop, this, timerId));
}

std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
    assert(m_timers.size() == m_activeTimers.size());
    std::vector<Entry> expired;
    Entry sentry = std::make_pair(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
    
    auto it = m_timers.lower_bound(sentry);
    assert(it == m_timers.end() || now < it->first);
    std::copy(m_timers.begin(), it, back_inserter(expired));
    m_timers.erase(m_timers.begin(), it);

    for (auto each : expired) {
        ActiveTimer timer(each.second, each.second->sequence());
        size_t n = m_activeTimers.erase(timer);
        assert(n == 1); (void)n;
    }
    assert(m_timers.size() == m_activeTimers.size());
    return expired;
}

void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
    Timestamp nextExpire;

    for (const auto& it : expired) {
        ActiveTimer timer(it.second, it.second->sequence());
        if (it.second->repeat() 
            && m_cancelingTimers.find(timer) == m_cancelingTimers.end()) {
            // 周期任务
            it.second->restart(now);
            insert(it.second);
        }
        else {
            // 一次性任务
            delete it.second;
        }
    }

    if (!m_timers.empty()) {
        nextExpire = m_timers.begin()->second->expiration();
    }

    if (clock::isValid(nextExpire)) {
        detail::resetTimerfd(m_timerfd, nextExpire);
    }
}

bool TimerQueue::insert(Timer* timer)
{
    m_loop->assertInLoopThread();
    assert(m_timers.size() == m_activeTimers.size());
    bool earliestChanged = false;
    Timestamp when = timer->expiration();

    auto it = m_timers.begin();
    if (it == m_timers.end() || when < it->first) {
        earliestChanged = true;
    }

    {
        auto result = m_timers.emplace(when, timer);
        assert(result.second); (void)result;
    }
    {
        auto result = m_activeTimers.emplace(timer, timer->sequence());
        assert(result.second); (void)result;
    }
    assert(m_timers.size() == m_activeTimers.size());
    return earliestChanged;
}

void TimerQueue::addTimerInLoop(Timer* timer)
{
    m_loop->assertInLoopThread();
    bool earliestChanged = insert(timer);

    if (earliestChanged) {
        // 有需要更早执行的任务，调整定时器的超时时间
        detail::resetTimerfd(m_timerfd, timer->expiration());
    }
}

void TimerQueue::cancelInLoop(TimerId timerId)
{
    m_loop->assertInLoopThread();
    assert(m_timers.size() == m_activeTimers.size());
    ActiveTimer timer(timerId.m_timer, timerId.m_sequence);
    auto it = m_activeTimers.find(timer);
    if (it != m_activeTimers.end()) {
        size_t n = m_timers.erase(Entry(it->first->expiration(), it->first));
        assert(n == 1); (void)n;
        delete it->first;
        m_activeTimers.erase(it);
    }
    else if (m_callingExpiredTimers) {
        m_cancelingTimers.insert(timer);
    }
    assert(m_timers.size() == m_activeTimers.size());
}

void TimerQueue::handleRead()
{
    m_loop->assertInLoopThread();
    Timestamp now = clock::now();
    detail::readTimerfd(m_timerfd, now);

    // 获取到期的定时器，并运行对应的任务
    auto expired = getExpired(now);

    m_callingExpiredTimers = true;
    m_cancelingTimers.clear();
    for (auto &it : expired) {
        it.second->run();
    }
    m_callingExpiredTimers = false;
    // 调整周期任务的下次运行时间
    reset(expired, now);
}

#elif defined WINDOWS
TimerQueue::TimerQueue(EventLoop* loop)
    : m_loop(loop)
{
    m_thd = std::make_shared<std::thread>(&TimerQueue::threadProc, this);
}

TimerQueue::~TimerQueue()
{
    stop();
    for (auto& it : m_timers) {
        delete it.second;
    }
}

TimerId TimerQueue::addTimer(const TimerCallback& cb, Timestamp when, double interval)
{
    Timer* timer = new Timer(cb, when, interval);
    //m_loop->assertInLoopThread();

    bool earliestChanged = insert(timer);
    if (earliestChanged) {
        // 有需要更早执行的任务
        m_cv.notify_all();
    }
    return TimerId(timer);
}

void TimerQueue::cancel(TimerId timerId)
{
    assert(m_timers.size() == m_activeTimers.size());
    ActiveTimer timer(timerId.m_timer, timerId.m_sequence);
    auto it = m_activeTimers.find(timer);
    if (it != m_activeTimers.end()) {
        size_t n = m_timers.erase(Entry(it->first->expiration(), it->first));
        assert(n == 1); (void)n;
        delete it->first;
        m_activeTimers.erase(it);
    }
    else if (m_callingExpiredTimers) {
        m_cancelingTimers.insert(timer);
    }
    assert(m_timers.size() == m_activeTimers.size());
}

std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
    assert(m_timers.size() == m_activeTimers.size());
    // 获取到时间的定时器
    std::vector<Entry> expired;
    Entry sentry = std::make_pair(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
    // 查找不小于目标值的第一个元素
    {
        // 需加锁
        std::unique_lock<std::mutex> lock(m_mutex);
        // 返回第一个未到期的 Timer 的迭代器
        auto it = m_timers.lower_bound(sentry);
        assert(it == m_timers.end() || now < it->first);
        // back_inserter 从容器尾部插入元素
        std::copy(m_timers.begin(), it, back_inserter(expired));
        m_timers.erase(m_timers.begin(), it);

        for (auto each : expired) {
            ActiveTimer timer(each.second, each.second->sequence());
            size_t n = m_activeTimers.erase(timer);
            assert(n == 1); (void)n;
        }
    }
    assert(m_timers.size() == m_activeTimers.size());
    return expired;
}

void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
    // 修改定时运行的定时任务的下次启动时间
    for (const auto& it : expired) {
        ActiveTimer timer(it.second, it.second->sequence());
        if (it.second->repeat()
            && m_cancelingTimers.find(timer) == m_cancelingTimers.end()) {
            // 周期任务
            it.second->restart(now);
            insert(it.second);
        }
        else {
            delete it.second;
        }
    }
}

bool TimerQueue::insert(Timer* timer)
{
    assert(m_timers.size() == m_activeTimers.size());
    bool earliestChanged = false;  // 最小时间的定时器是否改变
    Timestamp when = timer->expiration();
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto it = m_timers.begin();
        if (it == m_timers.end() || when < it->first) {
            // 如果定时器集合为空，或者小于第一个定时器的启动时间
            // 则表明最小时间更改
            earliestChanged = true;
        }
        {
            auto result = m_timers.emplace(when, timer);
            assert(result.second); (void)result;
        }
        {
            auto result = m_activeTimers.emplace(timer, timer->sequence());
            assert(result.second); (void)result;
        }
    }
    assert(m_timers.size() == m_activeTimers.size());
    return earliestChanged;
}

void TimerQueue::threadProc()
{
    assert(!m_isShutdown);
    while (!m_isShutdown) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_timers.empty()) {
            m_cv.wait(lock);
            continue;
        }
        auto it = m_timers.begin();
        assert(it != m_timers.end());
        auto diff = it->second->expiration() - clock::now();
        if (diff.count() > 0) {
            // 等待定时器到期，或有新任务(更早执行)加入
            m_cv.wait_for(lock, diff);
            continue;
        }
        else {
            Timestamp now = clock::now();
            // 释放锁，getExpired 内部用锁，不释放会死锁，
            // 并且之后的操作不在需要加锁
            lock.unlock();

            // 获取到期的任务
            std::vector<Entry> expired = getExpired(now);
            // 运行任务
            m_callingExpiredTimers = true;
            m_cancelingTimers.clear();
            for (const auto& it : expired) {
                it.second->run();
            }
            m_callingExpiredTimers = false;
            // 调整周期任务的下次运行时间
            reset(expired, now);
        }
    }
}
#endif