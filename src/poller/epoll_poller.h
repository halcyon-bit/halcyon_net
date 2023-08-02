#pragma once

#include "../poller.h"

#include <vector>

struct epoll_event;

namespace ephemeral::net
{
    /*
     * @brief   ::epoll(4)
     * @ps      在并发连接数较大而活动连接比例不高时，epoll(4) 比 poll(2) 更高效
     */
    class EPollPoller : public Poller
    {
    public:
        EPollPoller(EventLoop *loop);

        ~EPollPoller() override;

        void poll(int timeoutMs, ChannelList *activeChannels) override;

        void updateChannel(Channel *channel) override;

        void removeChannel(Channel *channel) override;

    private:
        void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;

        void update(int operation, Channel *channel);

        int getPollEvent(int event) const;

        int parsePollEvent(int event) const;

    private:
        using EPollFdList = std::vector<struct epoll_event>;

        EPollFdList m_epollfds;
        int m_epollfd; // for ::epol_create
    };
}