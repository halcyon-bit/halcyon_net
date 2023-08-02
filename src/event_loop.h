#pragma once

#include "callback.h"
#include "timer_id.h"

#include "platform.h"
#include "base/noncopyable.h"
#include "time/timestamp.h"

#include <thread>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

namespace ephemeral::net
{
    class Poller;
    class Channel;
    class TimerQueue;

    /*
     * @brief   one loop per thread
     *          每个线程只能有一个 EventLoop 对象
     *          创建了 EventLoop 对象的线程是 IO 线程
     */
    class EventLoop : base::noncopyable
    {
    public:
        /*
         * @brief   用户任务回调
         */
        using Functor = std::function<void()>;

    public:
        /*
         * @brief   构造函数，会检查当前线程是否已经创建了其
         *        他 EventLoop 对象，若已经创建则程序中止。
         *          内部会记住本对象所属的线程
         */
        EventLoop();

        /*
         * @brief   析构函数
         */
        ~EventLoop();

    public:
        /*
         * @brief   事件循环
         * @ps      事件循环必须在 IO 线程中执行，内部会检测
         */
        void loop();

        /*
         * @brief   退出事件循环
         */
        void quit();

        /*
         * @brief   在 IO 线程中执行某个用户任务回调，如果用户在
         *        当前 IO 线程调用这个函数，回调会同步进行；如果
         *        用户在其他线程调用，cb 会加入队列，IO 线程会被
         *        唤醒来调用这个 Functor。
         *          这样可以在不用锁的情况下保证线程安全性。
         */
        void runInLoop(const Functor& cb);

        /*
         * @brief   将 cb 放入队列，并在必要时唤醒 IO 线程
         */
        void queueInLoop(const Functor& cb);

        /*
         * @brief       在某个时间点运行定时器(运行一次)
         * @param[in]   时间点(绝对时间)
         * @param[in]   定时器回调函数
         * @return      标识
         */
        TimerId runAt(const base::Timestamp& time, const TimerCallback& cb);

        /*
         * @brief       延时运行定时器(运行一次)
         * @param[in]   时间(s)(相对时间)
         * @param[in]   定时器回调函数
         * @return      标识
         */
        TimerId runAfter(double delay, const TimerCallback& cb);

        /*
         * @brief       周期运行定时器
         * @param[in]   周期(s)(相对时间)
         * @param[in]   定时器回调函数
         * @return      标识
         */
        TimerId runLoop(double interval, const TimerCallback& cb);

        /*
         * @brief       取消定时器
         * @param[in]   定时器标识
         */
        void cancel(TimerId timerId);
        
        /*
         * @brief   唤醒 IO 线程
         */
        void wakeup();

        /*
         * @brief   更新 Poller 中的 Channel
         */
        void updateChannel(Channel* channel);

        /*
         * @brief   移除 Poller 中的 Channel
         */
        void removeChannel(Channel* channel);

#ifdef _CHECK
        bool hasChannel(Channel* channel);
#endif

        /*
         * @brief   assert 是否是 IO 线程
         */
        void assertInLoopThread()
        {
            if (!isInLoopThread()) {
                abortNotInLoopThread();
            }
        }

        /*
         * @brief   判断当前线程是否为 IO 线程
         */
        bool isInLoopThread() const
        {
            return m_threadId == std::this_thread::get_id();
        }

        /*
         * @brief   获取当前线程的 EventLoop，若没有，返回 nullptr
         */
        static EventLoop* getEventLoopOfCurrentThread();

    private:
        /*
         * @brief   抛出异常，当前线程非 IO 线程(abort())
         */
        void abortNotInLoopThread();

        /*
         * @brief   处理 wakeup 文件描述符上的读事件
         */
        void handleRead();

        /*
         * @brief   执行用户任务回调
         */
        void handleFunctors();

    private:
        using ChannelList = std::vector<Channel*>;

        bool m_looping{ false };  // 是否启动事件循环
        std::atomic<bool> m_quit{ false };  // 退出标志

        const std::thread::id m_threadId;  // 记录创建该 EventLoop 的线程 id

        std::unique_ptr<Poller> m_poller;  // Poller
        ChannelList m_activeChannels;  // 有活跃事件的 Channel

        std::unique_ptr<TimerQueue> m_timerQueue;  // 定时器

#ifdef WINDOWS
        int m_wakeupFd[2];  // wakeup 描述符
#elif defined LINUX
        int m_wakeupFd;  // wakeup 描述符
#endif
        std::unique_ptr<Channel> m_wakeupChannel;  // 处理 m_wakeupFd 上的可读事件

        bool m_callingFunctors{ false };  // 是否在执行用户任务回调
        std::mutex m_mutex;  // for m_functors
        std::vector<Functor> m_functors;  // 会暴露给其他线程，需要加锁
    };
}