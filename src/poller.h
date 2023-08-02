#pragma once

#include "base/noncopyable.h"
#include "event_loop.h"

#include <map>
#include <vector>

namespace ephemeral::net
{
    class Channel;

    /*
     * @brief   IO multiplexing 基类
     *          Poller 是 EventLoop 的间接成员，只供其 owner
     *        EventLoop 在IO线程调用，无需加锁。
     *          只负责 IO multiplexing，不负责事件分发。
     * @ps      虽然内部由 ChannelMap，但是 Poller 并不拥有 Channel，
     *        Channel 在析构之前必须自己 unregister（EventLoop::removeChannel())
     */
    class Poller : base::noncopyable
    {
    public:
        using ChannelList = std::vector<Channel*>;

    public:
        /*
         * @brief   构造函数
         */
        Poller(EventLoop* loop);

        /*
         * @brief   析构函数
         */
        virtual ~Poller();

        /*
         * @brief       调用 IO multiplexing 获得当前活动的 IO 事件
         * @param[in]   超时时间
         * @param[out]  活动的 IO 事件
         * @return
         * @ps          只能在 IO 线程中调用
         */
        virtual void poll(int timeoutMs, ChannelList* activeChannels) = 0;

        /*
         * @brief       更新 Channel，维护和更新 m_channels 数组及内部其他成员
         * @param[in]   需要被更新的 Channel
         * @ps          只能在 IO 线程中调用
         */
        virtual void updateChannel(Channel* channel) = 0;

        /*
         * @brief   移除 Channel
         * @ps      只能在 IO 线程中调用
         */
        virtual void removeChannel(Channel* channel) = 0;

#ifdef _CHECK
        /*
         * @brief   判断 Poller 中是否有 Channel
         */
        virtual bool hasChannel(Channel* channel) const;
#endif

        /*
         * @brief   获取 IO multiplexing
         *        Linux 使用 poll，Windows 使用 select
         */
        static Poller* newDefaultPoller(EventLoop* loop);

        /*
         * @brief   判定是否运行在 IO 线程
         */
        void assertInLoopThread() const
        {
            m_ownerLoop->assertInLoopThread();
        }

    protected:
        using ChannelMap = std::map<int, Channel*>;
        ChannelMap m_channels;  // 从 fd 到 Channel* 的映射

    private:
        EventLoop* m_ownerLoop;
    };
}