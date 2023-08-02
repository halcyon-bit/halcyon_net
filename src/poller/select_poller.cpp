#include "select_poller.h"
#include "../channel.h"

#include "log/logging.h"

#include <cassert>

using namespace ephemeral::net;

SelectPoller::SelectPoller(EventLoop* loop)
    : Poller(loop)
{
    m_fdset = new fd_set[FDSET_NUMBER];
    m_fdsetBackup = new fd_set[FDSET_NUMBER];

    FD_ZERO(&m_fdset[FDSET_TYPE_READ]);
    FD_ZERO(&m_fdset[FDSET_TYPE_WRITE]);
    FD_ZERO(&m_fdset[FDSET_TYPE_EXCEPTION]);

    FD_ZERO(&m_fdsetBackup[FDSET_TYPE_READ]);
    FD_ZERO(&m_fdsetBackup[FDSET_TYPE_WRITE]);
    FD_ZERO(&m_fdsetBackup[FDSET_TYPE_EXCEPTION]);
}

SelectPoller::~SelectPoller()
{
    assert(m_fdset != nullptr);
    if (nullptr != m_fdset) {
        delete[]m_fdset;
    }

    assert(m_fdsetBackup != nullptr);
    if (nullptr != m_fdsetBackup) {
        delete[]m_fdsetBackup;
    }
}

void SelectPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
    memcpy(m_fdset, m_fdsetBackup, sizeof(fd_set) * 3);
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int maxSockfd = -1;
    auto it = m_sockfdSet.rbegin();
    if (it == m_sockfdSet.rend()) {
        // 没有需要关注的 socket
        std::this_thread::sleep_for(100ms);
    }
    else {
        maxSockfd = *it;
    }

    int numEvents = ::select(maxSockfd + 1, &m_fdset[FDSET_TYPE_READ],
        &m_fdset[FDSET_TYPE_WRITE], &m_fdset[FDSET_TYPE_EXCEPTION], &tv);
    if (numEvents > 0) {
        LOG_TRACE << numEvents << " events happended";
        fillActiveChannels(numEvents, activeChannels);
    }
    else if (numEvents == 0) {
        LOG_TRACE << "nothing happended";
    }
    else {
        LOG_SYSERR << "SelectPoller::poll";
    }
}

void SelectPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const
{
    // 遍历 m_selectfds，找出有活动事件的fd，把它对应的 Channel 填入 activeChannels。
    // 复杂度：O(N)
    for (auto& it : m_selectfds) {
        if (numEvents <= 0) {
            break;
        }
        int event = EVENT_TYPE_NONE;
        if (FD_ISSET(it, &m_fdset[FDSET_TYPE_READ])) {
            event |= EVENT_TYPE_READ;
        }

        if (FD_ISSET(it, &m_fdset[FDSET_TYPE_WRITE])) {
            event |= EVENT_TYPE_WRITE;
        }

        if (FD_ISSET(it, &m_fdset[FDSET_TYPE_EXCEPTION])) {
            event |= EVENT_TYPE_ERROR;
        }

        if (event != EVENT_TYPE_NONE) {
            --numEvents;
            auto ch = m_channels.find(it);
            assert(ch != m_channels.end());
            Channel* channel = ch->second;
            assert(channel->fd() == it);
            // 设置活跃事件
            channel->setRevents(event);
            activeChannels->push_back(channel);
        }
    }
}

void SelectPoller::updateChannel(Channel* channel)
{
    assertInLoopThread();
    LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events();
    if (channel->index() < 0) {
        // 添加
        assert(m_channels.find(channel->fd()) == m_channels.end());
        int sockfd = channel->fd();
        // 设置关注的事件
        setSelectEvent(sockfd, channel->events());
        m_selectfds.push_back(sockfd);

        int idx = static_cast<int>(m_selectfds.size()) - 1;
        channel->setIndex(idx);
        m_channels[sockfd] = channel;
        m_sockfdSet.insert(sockfd);
    }
    else {
        // 更新
        assert(m_channels.find(channel->fd()) != m_channels.end());
        assert(m_channels[channel->fd()] == channel);
        int idx = channel->index();
        assert(0 <= idx && idx < static_cast<int>(m_selectfds.size()));
        int sockfd = m_selectfds[idx];
        assert(sockfd == channel->fd());

        setSelectEvent(sockfd, channel->events());
    }
}

void SelectPoller::removeChannel(Channel* channel)
{
    assertInLoopThread();
    LOG_TRACE << "fd = " << channel->fd();
    assert(m_channels.find(channel->fd()) != m_channels.end());
    assert(m_channels[channel->fd()] == channel);
    assert(channel->isNoneEvent());
    int idx = channel->index();
    assert(0 <= idx && idx < static_cast<int>(m_selectfds.size()));
    const int sockfd = m_selectfds[idx];
    assert(sockfd == channel->fd());
    m_sockfdSet.erase(sockfd);

    size_t n = m_channels.erase(channel->fd());
    assert(n == 1); (void)n;  // 主要作用就是避免应变量n未使用编译器警告
    // 从 m_pollfds 中移除
    if (static_cast<size_t>(idx) == m_selectfds.size() - 1) {
        // 如果在数组的最后则直接移除
        m_selectfds.pop_back();
    }
    else {
        // 如果不是最后一个，则和最后一个进行交换，再移除
        int channelAtEnd = m_selectfds.back();
        iter_swap(m_selectfds.begin() + idx, m_selectfds.end() - 1);
        assert(channelAtEnd >= 0);
        // 更新被替换的 Channel
        m_channels[channelAtEnd]->setIndex(idx);
        m_selectfds.pop_back();
    }
}

void SelectPoller::setSelectEvent(int sockfd, int event)
{
    if (event & EVENT_TYPE_READ) {
        FD_SET(sockfd, &m_fdsetBackup[FDSET_TYPE_READ]);
    }
    else {
        FD_CLR(sockfd, &m_fdsetBackup[FDSET_TYPE_READ]);
    }

    if (event & EVENT_TYPE_WRITE) {
        FD_SET(sockfd, &m_fdsetBackup[FDSET_TYPE_WRITE]);
    }
    else {
        FD_CLR(sockfd, &m_fdsetBackup[FDSET_TYPE_WRITE]);
    }

    if (event & EVENT_TYPE_ERROR) {
        FD_SET(sockfd, &m_fdsetBackup[FDSET_TYPE_EXCEPTION]);
    }
    else {
        FD_CLR(sockfd, &m_fdsetBackup[FDSET_TYPE_EXCEPTION]);
    }
}