#include "epoll_poller.h"
#include "../channel.h"

#include "log/logging.h"

#include <cassert>
#include <sys/epoll.h>

using namespace ephemeral::net;

static constexpr int g_initEPollListSize = 16;

EPollPoller::EPollPoller(EventLoop* loop)
    : Poller(loop)
    , m_epollfds(g_initEPollListSize)
{
    m_epollfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (m_epollfd < 0) {
        LOG_SYSFATAL << "EPollPoller::EPollPoller";
    }
}

EPollPoller::~EPollPoller()
{
    ::close(m_epollfd);
}

void EPollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
    int numEvents = ::epoll_wait(m_epollfd, &*m_epollfds.data(), m_epollfds.size(), timeoutMs);
    if (numEvents > 0) {
        LOG_TRACE << numEvents << " events happended";
        fillActiveChannels(numEvents, activeChannels);
        if (static_cast<size_t>(numEvents) == m_epollfds.size()) {
            m_epollfds.resize(m_epollfds.size() * 2);
        }
    }
    else if (numEvents == 0) {
        LOG_TRACE << "nothing happended";
    }
    else {
        LOG_SYSERR << "EPollPoller::poll()";
    }
}

void EPollPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const
{
    assert(static_cast<size_t>(numEvents) <= m_epollfds.size());
    for (int i = 0; i < numEvents; ++i) {
        Channel* channel = static_cast<Channel*>(m_epollfds[i].data.ptr);
#ifndef
        int fd = channel->fd();
        auto it = m_channels.find(fd);
        assert(it != m_channels.end());
        assert(it->second == channel);
#endif
        channel->setRevents();
        activeChannels->push_back(channel);
    }
}

void EPollPoller::updateChannel(Channel * channel)
{
    // 负责维护和更新 m_pollfds 数组
    // 添加新的 Channel 的复杂度为O(logN)，更新已有的 Channel 为O(1)
    // 因 Channel 记录了自己在 m_pollfds 数组中的下标，可以快速定位
    assertInLoopThread();
    LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events();

    if (channel->index() < 0) {
        assert(m_channels.find(channel->fd()) == m_channels.end());
        struct pollfd pfd;
        pfd.fd = channel->fd();
        pfd.events = static_cast<short>(getPollEvent(channel->events()));
        pfd.revents = 0;
        m_pollfds.push_back(pfd);

        int idx = static_cast<int>(m_pollfds.size()) - 1;
        channel->setIndex(idx);
        m_channels[pfd.fd] = channel;
    }
    else {
        // 更新
        // 如果某个 Channel 暂时不关心任何事件，就把 pollfd.fd 设置为 channel->fd() 的相反数减一。
        assert(m_channels.find(channel->fd()) != m_channels.end());
        assert(m_channels[channel->fd()] == channel);
        int idx = channel->index();
        assert(0 <= idx && idx < static_cast<int>(m_pollfds.size()));
        struct pollfd& pfd = m_pollfds[idx];
        assert(pfd.fd == channel->fd() || pfd.fd == -channel->fd() - 1);
        pfd.events = static_cast<short>(getPollEvent(channel->events()));
        pfd.revents = 0;
        if (channel->isNoneEvent()) {
            pfd.fd = -channel->fd() - 1;
        }
    }
}

void EPollPoller::removeChannel(Channel* channel)
{
    assertInLoopThread();
    LOG_TRACE << "fd = " << channel->fd();
    assert(m_channels.find(channel->fd()) != m_channels.end());
    assert(m_channels[channel->fd()] == channel);
    assert(channel->isNoneEvent());
    int idx = channel->index();
    assert(0 <= idx && idx < static_cast<int>(m_pollfds.size()));
    const struct pollfd& pfd = m_pollfds[idx]; (void)pfd;
    assert(pfd.fd == -channel->fd() - 1 && pfd.events == getPollEvent(channel->events()));

    size_t n = m_channels.erase(channel->fd());
    assert(n == 1); (void)n;  // 主要作用就是避免应变量n未使用编译器警告
    // 从 m_pollfds 中移除
    if (static_cast<size_t>(idx) == m_pollfds.size() - 1) {
        // 如果在数组的最后则直接移除
        m_pollfds.pop_back();
    }
    else {
        // 如果不是最后一个，则和最后一个进行交换，再移除
        int channelAtEnd = m_pollfds.back().fd;
        iter_swap(m_pollfds.begin() + idx, m_pollfds.end() - 1);
        if (channelAtEnd < 0) {
            // 为不关心任何事件的描述符，需要转化下
            channelAtEnd = -channelAtEnd - 1;
        }
        // 更新被替换的 Channel
        m_channels[channelAtEnd]->setIndex(idx);
        m_pollfds.pop_back();
    }
}

inline int EPollPoller::getPollEvent(int event) const
{
    int ret = 0;
    if (event == EVENT_TYPE_NONE) {
        return ret;
    }

    if (event & EVENT_TYPE_READ) {
        ret |= (POLLIN | POLLPRI);
    }

    if (event & EVENT_TYPE_WRITE) {
        ret |= POLLOUT;
    }
    return ret;
}

inline int EPollPoller::parsePollEvent(int event) const
{
    int ret = 0;
    if (event & POLLNVAL) {
        ret = EVENT_TYPE_NONE;
    }

    if ((event & POLLHUP) && !(event & POLLIN)) {
        ret |= EVENT_TYPE_CLOSE;
    }

    if (event & (POLLERR | POLLNVAL)) {
        ret |= EVENT_TYPE_ERROR;
    }

    if (event & (POLLIN | POLLPRI | POLLRDHUP)) {
        ret |= EVENT_TYPE_READ;
    }

    if (event & POLLOUT) {
        ret |= EVENT_TYPE_WRITE;
    }
    return ret;
}