#pragma once

#include "channel.h"
#include "socket.h"

/*
 * @brief   用户不可见类
 */
namespace ephemeral::net
{
    class EventLoop;
    class InetAddress;

    /*
     * @brief   用于 accept 新 TCP 连接，仅供 TcpServer 使用，
     *        TcpServer 的成员，生命周期由后者控制
     */
    class Acceptor : base::noncopyable
    {
    public:
        /*
         * @brief       有新连接时的回调
         * param[in]    客户端 socket
         * param[in]    客户端地址
         *
         */
        using NewConnectionCallback = std::function<void(int sockfd, const InetAddress&)>;

    public:
        /*
         * @brief       构造函数
         * @param[in]   EventLoop 对象
         * @param[in]   监听地址
         * @param[in]   启用/禁用 SO_REUSEPORT
         */
        Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport);

        /*
         * @brief   析构函数
         */
        ~Acceptor();

        /*
         * @brief   设置处理新连接的回调函数
         */
        void setNewConnectionCallback(const NewConnectionCallback& cb)
        {
            m_newConnectionCallback = cb;
        }

        /*
         * @brief   是否已经开始监听
         */
        bool listenning() const
        {
            return m_listen;
        }

        /*
         * @brief   开始监听 socket
         * @ps      在 IO 线程中运行
         */
        void listen();

    private:
        /*
         * @brief   处理监听 socket 的可读事件
         *          调用 accept，接受新连接，并回调处理函数
         *          (即调用 TcpServer 的 handleConnection)
         * @ps      在 IO 线程中运行
         */
        void handleRead();

    private:
        EventLoop* m_loop;
        Socket m_acceptSocket;  // listen socket
        Channel m_acceptChannel;  // 用于观察 m_acceptSocket 上的可读事件(即新连接的到来)
        NewConnectionCallback m_newConnectionCallback;  // 处理新连接的回调
        bool m_listen{ false };  // 是否开启监听
    };
}