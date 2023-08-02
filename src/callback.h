#pragma once

#include <memory>
#include <functional>

namespace ephemeral::net
{
    // 定时器回调
    using TimerCallback = std::function<void()>;

    // 连接
    class TcpConnection;
    using TcpConnectionSPtr = std::shared_ptr<TcpConnection>;

    class Buffer;
    // 连接回调
    using ConnectionCallback = std::function<void(const TcpConnectionSPtr&)>;
    // 信息回调
    using MessageCallback = std::function<void(const TcpConnectionSPtr&, Buffer* buf)>;
    // 写完成回调
    using WriteCompleteCallback = std::function<void(const TcpConnectionSPtr&)>;
    // 关闭回调
    using CloseCallback = std::function<void(const TcpConnectionSPtr&)>;
    //
    using HighWaterMarkCallback = std::function<void(const TcpConnectionSPtr&, size_t)>;

    void defaultConnectionCallback(const TcpConnectionSPtr& conn);
    void defaultMessageCallback(const TcpConnectionSPtr& conn, Buffer* buffer);
}