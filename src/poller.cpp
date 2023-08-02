#include "poller.h"

using namespace ephemeral::net;

Poller::Poller(EventLoop* loop)
    : m_ownerLoop(loop)
{}

Poller::~Poller() = default;

#ifdef _CHECK
bool Poller::hasChannel(Channel* channel) const
{
    assertInLoopThread();
    auto it = m_channels.find(channel->fd());
    return it != m_channels.end() && it->second == channel;
}
#endif