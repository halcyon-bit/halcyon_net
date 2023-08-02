#include "../poller.h"
#include "poll_poller.h"
#include "select_poller.h"

#include "platform.h"

using namespace ephemeral::net;

Poller* Poller::newDefaultPoller(EventLoop* loop)
{
#ifdef WINDOWS
    return new SelectPoller(loop);
#elif defined LINUX
    return new PollPoller(loop);
#endif
    }
