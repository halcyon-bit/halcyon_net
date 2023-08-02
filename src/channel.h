#pragma once

#include "base/noncopyable.h"

#include <functional>
#include <memory>

/*
 * @brief   用户不可见类
 */
namespace ephemeral::net
{
    class EventLoop;

    // 事件
    enum EventType
    {
        EVENT_TYPE_NONE = 0x0000,
        EVENT_TYPE_READ = 0x0001,
        EVENT_TYPE_WRITE = 0x0002,
        EVENT_TYPE_ERROR = 0x0004,
        EVENT_TYPE_CLOSE = 0x0008,
    };

    /*
     * @brief   将 IO multiplexing 拿到的 IO 事件分发
     *        给各个文件描述符(fd)的事件处理函数
     *          每个 Channel 对象自始至终只属于一个 EventLoop，
     *        因此每个 Channel 对象都只属于某一个 IO 线程。
     *          每个 Channel 对象自始至终只负责一个文件描述符的
     *        IO 事件分发，但它并不拥有这个 fd，析构时也不关闭。
     *          Channel 的生命期由其 owner class 负责管理，它一般是
     *        其他 class 的直接或间接成员。
     *          成员函数都只能在 IO 线程调用，因此更新数据不需要加锁。
     */
    class Channel : base::noncopyable
    {
    public:
        // 事件回调
        using EventCallback = std::function<void()>;

    public:
        /*
         * @brief       构造函数
         * @param[in]   EventLoop 对象
         * @param[in]   文件描述符
         */
        Channel(EventLoop* loop, int fd);

        /*
         * @brief   析构函数
         */
        ~Channel();

        /*
         * @brief   事件处理函数(调用相应的回调)
         *          由 EventLoop::loop() 调用
         */
        void handleEvent();

        /*
         * @brief   设置读事件的回调
         */
        void setReadCallback(const EventCallback& cb)
        {
            m_readCallback = cb;
        }

        /*
         * @brief   设置写事件的回调
         */
        void setWriteCallback(const EventCallback& cb)
        {
            m_writeCallback = cb;
        }

        /*
         * @brief   设置错误事件的回调
         */
        void setErrorCallback(const EventCallback& cb)
        {
            m_errorCallback = cb;
        }

        /*
         * @brief   设置断线事件的回调
         */
        void setCloseCallback(const EventCallback& cb)
        {
            m_closeCallback = cb;
        }

        /*
         * @brief   获取文件描述符
         */
        int fd() const
        {
            return m_fd;
        }

        /*
         * @brief   获取当前订阅的事件(多个事件与的结果)
         */
        int events() const
        {
            return m_events;
        }

        /*
         * @brief       设置活跃的事件(由 Poller 调用)
         * @param[in]   活跃的事件
         */
        void setRevents(int revent)
        {
            m_revents = revent;
        }

        /*
         * @brief   是否没有订阅任何事件
         */
        bool isNoneEvent() const
        {
            return m_events == EVENT_TYPE_NONE;
        }

        /*
         * @brief   订阅读事件
         */
        void enableRead()
        {
            m_events |= EVENT_TYPE_READ;
            update();
        }

        /*
         * @brief   取消订阅读事件
         */
        void disableRead()
        {
            m_events &= ~EVENT_TYPE_READ;
            update();
        }

        /*
         * @brief   订阅写事件
         */
        void enableWrite()
        {
            m_events |= EVENT_TYPE_WRITE;
            update();
        }

        /*
         * @brief   取消订阅写事件
         */
        void disableWrite()
        {
            m_events &= ~EVENT_TYPE_WRITE;
            update();
        }

        /*
         * @brief   取消订阅所有事件
         */
        void disableAll()
        {
            m_events = EVENT_TYPE_NONE;
            update();
        }

        /*
         * @brief   是否订阅读事件
         */
        bool isReading() const
        {
            return m_events & EVENT_TYPE_READ;
        }

        /*
         * @brief   是否订阅写事件
         */
        bool isWriting() const
        {
            return m_events & EVENT_TYPE_WRITE;
        }

        /*
         * @brief   获取 Channel 在 Poller::**fds 数组的下标
         * @return  位置
         * @ps      供 Poller 使用
         */
        int index()
        {
            return m_index;
        }

        /*
         * @brief       设置 Channel 在 Poller::**fds 数组的下标
         * @param[in]   位置
         * @ps          供 Poller 使用
         */
        void setIndex(int idx)
        {
            m_index = idx;
        }

        /*
         * @brief   获取 EventLoop 对象
         */
        EventLoop* ownerLoop()
        {
            return m_loop;
        }

        /*
         * @brief   for TcpConnection，判断或延续 TcpConnection 的生命
         *          chanel 总是其他类的成员变量，像Acceptor，TcpConnection。
         *          TcpConnection 是被 std::shared_ptr 管理的，所以可能在处
         *          理事件时被销毁，所以内部使用 weak_ptr 管理，在处理时，提
         *          升至 shared_ptr，这样可以确保程序正常。
         */
        void tie(const std::shared_ptr<void>&);

        /*
         * @brief   从 Poller 的 Channel 列表中移除
         * @ps      调用之前需要取消所有事件的订阅
         *          Chanel 被析构前，需要调用该函数
         */
        void remove();

    private:
        /*
         * @brief   更新 Poller 中的 Channel
         *          当有新的事件订阅或取消订阅时，需更新 Channel。
         */
        void update();

        /*
         * @brief   事件处理函数(调用相应的回调)
         */
        void handleEventWithGuard();

    private:
        EventLoop* m_loop;
        const int m_fd;  // 文件描述符

        int m_events{ 0 };  // 关心的 IO 事件
        int m_revents{ 0 };  // 目前活动的 IO 事件，由 Poller 设置
        int m_index{ -1 };  // used by Poller

        bool m_eventHandling{ false };  // 是否在处理事件函数

        // for TcpConnection
        std::weak_ptr<void> m_tie;
        bool m_isTie{ false };

        EventCallback m_readCallback;  // 读回调
        EventCallback m_writeCallback;  // 写回调
        EventCallback m_errorCallback;  // 错误处理回调
        EventCallback m_closeCallback;  // 关闭事件回调
#ifdef _CHECK
        bool m_addedToLoop{ false };  // Channel 是否存在于 Poller 中
#endif
    };
}