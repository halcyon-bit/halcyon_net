#pragma once

#include "time/timestamp.h"
#include "callback.h"
#include "channel.h"

#include "platform.h"
#include "base/noncopyable.h"

#include <set>
#include <vector>
#include <memory>

#ifdef WINDOWS
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <atomic>
#endif

namespace ephemeral::net
{
    class EventLoop;
    class Timer;
    class TimerId;

    /*
     * @brief   定时器
     * @ps      Linux 中 TimerQueue 的成员函数只能在其所属的
     *        IO 线程调用，因此不必加锁。
     */
    class TimerQueue : base::noncopyable
    {
    public:
        /*
         * @brief   构造函数
         */
        TimerQueue(EventLoop* loop);

        /*
         * @brief   析构函数
         */
        ~TimerQueue();

        /*
         * @brief       添加定时器
         * @param[in]   回调函数
         * @param[in]   启动时间
         * @param[in]   周期时间，0 表示只运行一次
         * @return      标识
         */
        TimerId addTimer(const TimerCallback& cb, base::Timestamp when, double interval);

        void cancel(TimerId timerId);

    private:
        // FIXME：use unique_ptr
        using Entry = std::pair<base::Timestamp, Timer*>;
        //using Entry = std::pair<base::Timestamp, std::unique_ptr<Timer*>>;

        /*
         * @brief       获取到时的定时器
         * @param[in]   时间
         * @return      到时的定时器集合
         */
        std::vector<Entry> getExpired(base::Timestamp now);

        /*
         * @brief       修改周期任务的下次运行时间，一次任务则删除
         * @param[in]   需要修改的定时器集合
         * @param[in]   时间
         */
        void reset(const std::vector<Entry>& expired, base::Timestamp now);

        /*
         * @brief       将定时器插入到 TimerList 中
         * @param[in]   定时器
         * @return      最小定时的定时器是否改变
         */
        bool insert(Timer* timer);

        void cancelInLoop(TimerId timerId);

#ifdef WINDOWS
        /*
         * @brief       运行定时器任务
         */
        void threadProc();

        /*
         * @brief       停止线程
         */
        void stop()
        {
            m_isShutdown.store(true);
            m_cv.notify_all();
            m_thd->join();
        }
#elif defined LINUX
        /*
         * @brief   确保 add Timer 在 IO 线程中，这样无需加锁保护
         * @ps      必须在 IO 线程中执行
         */
        void addTimerInLoop(Timer* timer);

        /*
         * @brief   处理到期的定时器
         */
        void handleRead();
#endif

    private:
        using TimerList = std::set<Entry>;

        using ActiveTimer = std::pair<Timer*, int64_t>;
        using ActiveTimerSet = std::set<ActiveTimer>;

        EventLoop* m_loop;
        TimerList m_timers;  // 定时器集合，定时器使用 set 保存
                             // key 为 pair<time, timer>，可以确保相同运行时间的定时器都存在
                             // set 可以很方便的获取到期的定时器。

#ifdef WINDOWS
        // windows 平台使用线程实现定时器
        std::shared_ptr<std::thread> m_thd;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::atomic<bool> m_isShutdown{ false };
#elif defined LINUX
        // linux 使用 timerfd_create 等系列函数
        // 可以用于 poll，epoll 等函数，方便统一代码风格
        const int m_timerfd;
        Channel m_timerfdChannel;
#endif
        // for cancel
        ActiveTimerSet m_activeTimers;  // 目前有效的 Timer，
                                        // 理论上 size() == m_timers.size()，数据相同
        // 用于"自注销"，即在定时器回调中注销当前定时器
        bool m_callingExpiredTimers{ false };
        ActiveTimerSet m_cancelingTimers;  // 
    };
}