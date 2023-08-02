#pragma once

#include "base/noncopyable.h"

#include <mutex>
#include <thread>
#include <memory>
#include <condition_variable>

/*
 * @brief   用户不可见类
 */
namespace ephemeral::net
{
    class EventLoop;

    /*
     * @brief   创建线程并运行 EventLoop
     */
    class EventLoopThread : base::noncopyable
    {
    public:
        /*
         * @brief   构造函数
         */
        EventLoopThread();

        /*
         * @brief   析构函数
         */
        ~EventLoopThread();

    public:
        /*
         * @brief   创建线程，并启动事件循环
         * @return  返回 EventLoop
         */
        EventLoop* startLoop();

    private:
        /*
         * @brief   线程函数
         */
        void threadFunc();

    private:
        EventLoop* m_loop{ nullptr };

        std::shared_ptr<std::thread> m_thd;
        std::mutex m_mutex;
        std::condition_variable m_cv;
    };
}