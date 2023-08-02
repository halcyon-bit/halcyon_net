#pragma once

#include "base/noncopyable.h"

#include <mutex>
#include <vector>
#include <thread>
#include <memory>
#include <condition_variable>

/*
 * @brief   用户不可见类
 */
namespace ephemeral::net
{
    class EventLoop;
    class EventLoopThread;

    /*
     * @brief   线程池(用于多线程 TcpServer)
     */
    class EventLoopThreadPool : base::noncopyable
    {
    public:
        /*
         * @brief   构造函数
         */
        EventLoopThreadPool(EventLoop* baseLoop);

        /*
         * @brief   析构函数
         */
        ~EventLoopThreadPool();

    public:
        /*
         * @brief   设置线程数量
         */
        void setThreadNum(int numThreads)
        {
            m_numThreads = numThreads;
        }

        /*
         * @brief   启动
         */
        void start();

        /*
         * @brief   获取一个 EventLoop
         */
        EventLoop* getNextLoop();

    private:
        EventLoop* m_baseLoop;  // 基础 EventLoop，若非多线程则 getNextLoop 返回该 EventLoop
        //bool m_started{ false };  // 是否启动
        int m_numThreads{ 0 };  // 线程数
        int m_next{ 0 };  // for getNextLoop

        std::vector<std::unique_ptr<EventLoopThread>> m_threads;
        std::vector<EventLoop*> m_loops;
    };
}