#include "event_loop_thread.h"
#include "event_loop.h"

#include <cassert>

using namespace ephemeral::net;

EventLoopThread::EventLoopThread()
{}

EventLoopThread::~EventLoopThread()
{
    if (m_loop != nullptr) {
        m_loop->quit();
        m_thd->join();
    }
}

EventLoop* EventLoopThread::startLoop()
{
    assert(m_thd == nullptr);
    m_thd = std::make_shared<std::thread>(&EventLoopThread::threadFunc, this);

    {
        // 等待 loop
        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_loop == nullptr) {
            m_cv.wait(lock);
        }
    }

    return m_loop;
}

void EventLoopThread::threadFunc()
{
    EventLoop loop;

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_loop = &loop;
        m_cv.notify_all();
    }
    loop.loop();

    {
        // 退出
        std::unique_lock<std::mutex> lock(m_mutex);
        m_loop = nullptr;
    }
}