#pragma once

#include "callback.h"
#include "tcp_connection.h"

#include <map>
#include <atomic>

/*
 * @brief   用户可见类
 */
namespace ephemeral::net
{
    class Acceptor;
    class EventLoop;
    class EventLoopThreadPool;

    /*
     * @brief   TcpServer 的功能是管理 accept 获得的 TcpConnection.
     *        供用户直接使用的，只需设置好 callback，再调用 start() 即可。
     *          使用 Acceptor 接受客户端的连接。
     *          支持单线程和多线程 server
     */
    class TcpServer : base::noncopyable
    {
    public:
        /*
         * @brief       构造函数
         * @param[in]   EventLoop 对象
         * @param[in]   监听地址
         * @param[in]   服务器名称
         * @param[in]   启用/禁用 SO_REUSEPORT
         */
        TcpServer(EventLoop* loop, const InetAddress& listenAddr, const std::string_view& name, bool reuseport);

        /*
         * @brief   析构函数
         */
        ~TcpServer();

        /*
         * @brief   获取服务器名称
         */
        const std::string& name() const
        {
            return m_name;
        }

        /*
         * @brief   设置线程数量
         * @ps      Accepts 的事件总在 loop 中运行(新连接的到来)
         *          0：所有的 I/O 事件都在 loop 中运行(构造函数传入的 loop)
         *          1：所有的 I/O 事件都在另一个线程中运行
         *          N：所有的 I/O 事件平均分配在每一个线程中
         */
        void setThreadNum(int numThreads);

        /*
         * @brief   启动 Tcp Server
         * @ps      线程安全
         */
        void start();

        /*
         * @brief   设置连接状态回调
         * @ps      非线程安全
         */
        void setConnectionCallback(const ConnectionCallback& cb)
        {
            m_connectionCallback = cb;
        }

        /*
         * @brief   设置信息回调函数
         * @ps      非线程安全
         */
        void setMessageCallback(const MessageCallback& cb)
        {
            m_messageCallback = cb;
        }

        /*
         * @brief   设置写完成回调函数
         * @ps      非线程安全
         */
        void setWriteCompleteCallback(const WriteCompleteCallback& cb)
        {
            m_writeCompleteCallback = cb;
        }

    private:
        /*
         * @brief       当有新连接时的回调函数，由 Acceptor 调用
         * @param[in]   文件描述符
         * @param[in]   网络地址
         * @ps          非线程安全函数，但是只在 IO 线程中调用，所以是安全的
         */
        void handleConnection(int sockfd, const InetAddress& peerAddr);

        /*
         * @brief       当有客户端离线时调用，由 TcpConnection 调用
         * @param[in]   断线的连接
         */
        void handleDisConnection(const TcpConnectionSPtr& conn);

        /*
         * @brief   在 m_loop 中处理断线事件
         */
        void handleDisConnectionInLoop(const TcpConnectionSPtr& conn);

    private:
        using ConnectionMap = std::map<std::string, TcpConnectionSPtr>;

        EventLoop* m_loop;
        const std::string m_name;  // 服务器名称

        std::unique_ptr<Acceptor> m_acceptor;  // Acceptor
        std::unique_ptr<EventLoopThreadPool> m_threadPool;  // 线程池

        ConnectionCallback m_connectionCallback;  // 连接回调
        MessageCallback m_messageCallback;  // 信息回调(读事件)
        WriteCompleteCallback m_writeCompleteCallback;  // 写完成回调

        std::atomic<bool> m_started{ false };  // 是否启动
        int m_nextConnId{ 1 };  // for TcpConnection

        ConnectionMap m_connections;  // TcpConnection 管理
    };
}