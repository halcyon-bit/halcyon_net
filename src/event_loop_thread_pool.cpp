#include "event_loop_thread_pool.h"
#include "event_loop.h"
#include "event_loop_thread.h"

#include <cassert>

using namespace ephemeral::net;

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop)
    : m_baseLoop(baseLoop)
{}

EventLoopThreadPool::~EventLoopThreadPool()
{}

void EventLoopThreadPool::start()
{
    //assert(!m_started);
    m_baseLoop->assertInLoopThread();

    //m_started = true;

    for (int i = 0; i < m_numThreads; ++i) {
        auto ptr = std::make_unique<EventLoopThread>();
        m_loops.push_back(ptr->startLoop());
        m_threads.push_back(std::move(ptr));
    }
}

EventLoop* EventLoopThreadPool::getNextLoop()
{
    m_baseLoop->assertInLoopThread();
    EventLoop* loop = m_baseLoop;

    if (!m_loops.empty()) {
        loop = m_loops[m_next];
        ++m_next;
        if (static_cast<size_t>(m_next) >= m_loops.size()) {
            m_next = 0;
        }
    }
    return loop;
}