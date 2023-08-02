#pragma once

namespace ephemeral::net
{
    class Timer;

    class TimerId
    {
    public:
        TimerId(Timer* timer = nullptr, int64_t seq = 0)
            : m_timer(timer)
            , m_sequence(seq)
        {}

        friend class TimerQueue;

    private:
        Timer* m_timer;
        int64_t m_sequence;  // 唯一序列号
    };
}