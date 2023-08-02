#pragma once

#include "inet_address.h"
#include "timer_id.h"

#include "base/noncopyable.h"

#include <functional>
#include <memory>

/*
 * @brief   用户不可见类
 */
namespace ephemeral::net
{
    class Channel;
    class EventLoop;

    class Connector : base::noncopyable, public std::enable_shared_from_this<Connector>
    {
    public:
        using NewConnectionCallback = std::function<void(int sockfd)>;

        Connector(EventLoop* loop, const InetAddress& serverAddr);
        ~Connector();

    public:
        void setNewConnectionCallback(const NewConnectionCallback& cb)
        {
            m_newConnectionCallback = cb;
        }

        /*
         * @brief
         * @param[in]
         * @param[out]
         * @return
         */
        void start();

        /*
         * @brief
         * @param[in]
         * @param[out]
         * @return
         * @ps      在IO线程中运行
         */
        void restart();

        /*
         * @brief
         * @param[in]
         * @param[out]
         * @return
         */
        void stop();

        /*
         * @brief   获取服务器地址
         */
        const InetAddress& serverAddress() const
        {
            return m_serverAddr;
        }

    private:
        enum States { kDisconnected, kConnecting, kConnected };

        void setState(States s)
        {
            m_state = s;
        }

        void startInLoop();

        void stopInLoop();

        void connect();

        void connecting(int sockfd);

        void handleWrite();

        void handleError();

        void retry(int sockfd);

        int removeAndResetChannel();

        void resetChannel();

    private:
        EventLoop* m_loop;
        InetAddress m_serverAddr;
        bool m_connect{ false };
        States m_state{ kDisconnected };
        std::unique_ptr<Channel> m_channel;
        NewConnectionCallback m_newConnectionCallback;
        int m_retryDelayMs{ initRetryDelayMs };
        TimerId m_timerId;

        static const int maxRetryDelayMs = 30000;
        static const int initRetryDelayMs = 500;
    };

    using ConnectorSPtr = std::shared_ptr<Connector>;
}