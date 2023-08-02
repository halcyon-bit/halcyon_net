#pragma once

#include "../poller.h"

#include <vector>

struct pollfd;

namespace ephemeral::net
{
    /*
     * @brief   ::poll(2)
     * @ps      poll 是 level trigger，需要在读事件中立即 read，否则
     *        下次会立刻触发。
     */
    class PollPoller : public Poller
    {
    public:
        /*
         * @brief   构造函数
         */
        PollPoller(EventLoop* loop);

        /*
         * @brief   析构函数
         */
        ~PollPoller() override;

        /*
         * @brief       调用 poll(2) 获得当前活动的 IO 事件
         * @param[in]   超时时间
         * @param[out]  活动的 IO 事件
         */
        void poll(int timeoutMs, ChannelList* activeChannels) override;

        /*
         * @brief   更新 ChannelMap、m_pollfds 数组
         */
        void updateChannel(Channel* channel) override;

        /*
         * @brief   从 ChannelMap、m_pollfds 数组中移除 Channel
         */
        void removeChannel(Channel* channel) override;

    private:
        /*
         * @brief   查找活动的 Channel 并填充 activeChannels
         */
        void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;

        /*
         * @brief   EventType -> poll event
         */
        int getPollEvent(int event) const;

        /*
         * @brief   poll event -> EventType
         */
        int parsePollEvent(int event) const;

    private:
        using PollFdList = std::vector<struct pollfd>;

        PollFdList m_pollfds;  // pollfd 列表，用于 ::poll()
    };
}