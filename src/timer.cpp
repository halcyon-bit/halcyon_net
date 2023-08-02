#include "timer.h"

using namespace ephemeral::base;
using namespace ephemeral::net;

std::atomic<int64_t> Timer::s_numCreated = 0;

void Timer::restart(Timestamp now)
{
    if (m_repeat) {
        m_expiration = clock::addTime(now, m_interval);
    }
    else {
        m_expiration = Timestamp();
    }
}