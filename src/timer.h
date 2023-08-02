#pragma once

#include "callback.h"

#include "base/noncopyable.h"
#include "time/timestamp.h"

#include <atomic>

namespace ephemeral::net
{
    class Timer : base::noncopyable
    {
    public:
        /*
         * @brief   构造函数
         */
        Timer(const TimerCallback& cb, base::Timestamp when, double interval)
            : m_callback(cb)
            , m_expiration(when)
            , m_interval(interval)
            , m_repeat(m_interval > 0.0)
            , m_sequence(++s_numCreated)
        {}

        /*
         * @brief   运行定时器处理函数
         */
        void run() const
        {
            m_callback();
        }

        /*
         * @brief   获取定时器的启动时间(到期时间)
         */
        base::Timestamp expiration() const
        {
            return m_expiration;
        }

        /*
         * @brief   是否为周期运行
         */
        bool repeat() const
        {
            return m_repeat;
        }

        /*
         * @brief   获取标识
         */
        int64_t sequence()
        {
            return m_sequence;
        }

        /*
         * @brief       重新启动定时器(周期任务，设置下次启动时间）
         */
        void restart(base::Timestamp now);

    private:
        const TimerCallback m_callback;  // 回调函数
        base::Timestamp m_expiration;  // 到期时间(绝对时间)
        const double m_interval;  // 间隔(周期任务)
        const bool m_repeat;  // 周期定时 或 单次定时
        const int64_t m_sequence;  // 标识

        static std::atomic<int64_t> s_numCreated;  // 标识生成器
    };
}