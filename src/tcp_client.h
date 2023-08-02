#pragma once

#include "tcp_connection.h"

#include <mutex>
#include <memory>

namespace ephemeral::net
{
    class Connector;
    using ConnectorSPtr = std::shared_ptr<Connector>;

    class TcpClient : base::noncopyable
    {
    public:
        TcpClient(EventLoop* loop, const InetAddress& serverAddr, const std::string_view& name);

        ~TcpClient();

        void connect();

        void disconnect();

        void stop();

        TcpConnectionSPtr connection() const
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_connection;
        }

        bool retry() const;

        void enableRetry()
        {
            m_retry = true;
        }

        const std::string& name() const
        {
            return m_name;
        }

        void setConnectionCallback(const ConnectionCallback& cb)
        {
            m_connectionCallback = cb;
        }

        void setMessageCallback(const MessageCallback& cb)
        {
            m_messageCallback = cb;
        }

        void setWriteCompleteCallback(const WriteCompleteCallback& cb)
        {
            m_writeCompleteCallback = cb;
        }

    private:
        void handleConnection(int sockfd);

        void handleDisConnection(const TcpConnectionSPtr& conn);

    private:
        EventLoop* m_loop;
        ConnectorSPtr m_connector;
        std::string m_name;

        ConnectionCallback m_connectionCallback;
        MessageCallback m_messageCallback;
        WriteCompleteCallback m_writeCompleteCallback;

        bool m_retry{ false };
        bool m_connect{ false };

        int m_nextConnId{ 1 };
        mutable std::mutex m_mutex;
        TcpConnectionSPtr m_connection;
    };
}