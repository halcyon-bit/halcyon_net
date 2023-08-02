#include "poll_poller.h"
#include "../channel.h"

#include "log/logging.h"

#include <cassert>
#include <poll.h>

using namespace ephemeral::net;

PollPoller::PollPoller(EventLoop* loop)
    : Poller(loop)
{}

PollPoller::~PollPoller() = default;

void PollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
    int numEvents = ::poll(&*m_pollfds.data(), m_pollfds.size(), timeoutMs);
    if (numEvents > 0) {
        LOG_TRACE << numEvents << " events happended";
        fillActiveChannels(numEvents, activeChannels);
    }
    else if (numEvents == 0) {
        LOG_TRACE << "nothing happended";
    }
    else {
        LOG_SYSERR << "PollPoller::poll()";
    }
}

void PollPoller::fillActiveChannels(int numEvents, ChannelList* activeChannels) const
{
    // 遍历 m_pollfds，找出有活动事件的 fd，把它对应的 Channel 填入 activeChannels。
    // 复杂度：O(N)
    for (const auto& it : m_pollfds) {
        if (it.revents > 0) {
            --numEvents;
            auto ch = m_channels.find(it.fd);
            assert(ch != m_channels.end());
            Channel* channel = ch->second;
            assert(channel->fd() == it.fd);
            // 设置活跃事件
            channel->setRevents(parsePollEvent(it.revents));
            activeChannels->push_back(channel);
        }
    }
}

void PollPoller::updateChannel(Channel * channel)
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
            // 不关心任何事件
            // 因为文件描述符0、1、2分别表示标准输入、标准输出、标准错误
            // 减一可忽略标准输入
            pfd.fd = -channel->fd() - 1;
        }
    }
}

void PollPoller::removeChannel(Channel* channel)
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

inline int PollPoller::getPollEvent(int event) const
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

inline int PollPoller::parsePollEvent(int event) const
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