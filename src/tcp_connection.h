#pragma once

#include "buffer.h"
#include "callback.h"
#include "inet_address.h"

#include "base/noncopyable.h"

namespace ephemeral::net
{
    class Socket;
    class Channel;
    class EventLoop;

    /*
     * @brief   一次 TCP 连接，不可再生，一旦断开连接，这个 TcpConnect
     *          对象就没啥用了，用户不需要创建该类对象，不能发起连接
     * @ps      使用 shared_ptr 管理
     */
    class TcpConnection : base::noncopyable, public std::enable_shared_from_this<TcpConnection>
    {
    public:
        /*
         * @brief       构造函数
         * @param[in]   EventLoop 对象
         * @param[in]   本次连接的名称
         * @param[in]   已经建立好连接的 socket fd
         * @param[in]   本机地址
         * @param[in]   远程地址
         */
        TcpConnection(EventLoop* loop, const std::string_view& name, int sockfd,
            const InetAddress& localAddr, const InetAddress& peerAddr);

        /*
         * @brief   析构函数
         */
        ~TcpConnection();

        /*
         * @brief   获取 EventLoop 对象
         */
        EventLoop* getLoop() const
        {
            return m_loop;
        }

        /*
         * @brief   获取本次连接的名称
         */
        const std::string& name() const
        {
            return m_name;
        }

        /*
         * @brief   获取本机地址
         */
        const InetAddress& localAddress()
        {
            return m_localAddr;
        }

        /*
         * @brief   获取远程地址
         */
        const InetAddress& peerAddress()
        {
            return m_peerAddr;
        }

        /*
         * @brief   是否已连接
         */
        bool connected() const
        {
            return m_state == kConnected;
        }

        /*
         * @brief   连接是否已断开
         */
        bool disconnected() const
        {
            return m_state == kDisconnected;
        }

        /*
         * @brief   发送数据(调用 sendInLoop)
         */
        void send(const std::string_view& message);
        void send(const void* message, int len);
        void send(Buffer* message);

        /*
         * @brief   仅关闭写连接(调用 shutdownInLoop)
         *        读连接为被动关闭
         */
        void shutdown();

        /*
         * @brief   关闭连接
         */
        void forceClose();
        void forceCloseWithDelay(double seconds);

        /*
         * @brief   启用/禁用 TCP_NODELAY (Nagle's algorithm)
         */
        void setTcpNoDelay(bool on);

        /*
         * @brief   启动数据读取
         */
        void startRead();

        /*
         * @brief   停止数据读取
         */
        void stopRead();

        /*
         * @brief   是否开启数据读取
         */
        bool isReading() const
        {
            return m_reading;
        }

        /*
         * @brief   设置连接回调
         */
        void setConnectionCallback(const ConnectionCallback& cb)
        {
            m_connectionCallback = cb;
        }

        /*
         * @brief   设置读事件的回调函数
         * @ps      用于读事件的处理，而写事件内部会自行处理
         */
        void setMessageCallback(const MessageCallback& cb)
        {
            m_messageCallback = cb;
        }

        /*
         * @brief   设置写完成回调
         */
        void setWriteCompleteCallback(const WriteCompleteCallback& cb)
        {
            m_writeCompleteCallback = cb;
        }

        /*
         * @brief
         * @param[in]
         * @param[out]
         * @return
         */
        void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)
        {
            m_highWaterMarkCallback = cb;
            m_highWaterMark = highWaterMark;
        }

    public:  // 供内部使用的接口
        /*
         * @brief   设置断线回调
         * @ps      供 TcpServer 和 TcpClient 使用，非用户
         *        用于通知它们移除所持有的 TcpConnectionSPtr;
         *        对于 TcpServer 是 handleDisConnection
         */
        void setCloseCallback(const CloseCallback& cb)
        {
            m_closeCallback = cb;
        }

        /*
         * @brief   用于新建连接时的处理，并通知用户
         * @ps      内部使用 for TcpServer
         */
        void connectEstablished();

        /*
         * @brief   用于连接断开时的处理，TcpConnection 析构前最后调
         *        用的一个成员函数，会通知用户连接已断开
         * @ps      内部使用 for TcpServer
         */
        void connectDestroyed();

    private:
        enum StateE { kConnecting, kConnected, kDisconnecting, kDisconnected, };

        /*
         * @brief   设置当前连接的状态
         */
        void setState(StateE s)
        {
            m_state = s;
        }

        /*
         * @brief   处理读事件
         */
        void handleRead();

        /*
         * @brief   处理写事件
         */
        void handleWrite();

        /*
         * @brief   处理错误事件
         */
        void handleError();

        /*
         * @brief   处理关闭事件
         */
        void handleClose();

        /*
         * @brief   发送数据
         */
        void sendInLoop(const void* data, size_t len);
        void sendInLoop(const std::string_view& message);

        /*
         * @brief   关闭连接通道(会等待未发送的数据发送出去)
         */
        void shutdownInLoop();

        /*
         * @brief   主动关闭连接
         */
        void forceCloseInLoop();

        /*
         * @brief   启动数据读取
         */
        void startReadInLoop();

        /*
         * @brief   停止关注读事件
         */
        void stopReadInLoop();

    private:
        EventLoop* m_loop;
        std::string m_name;  // 本次连接的名称
        StateE m_state{ kConnecting };  // 连接的状态，初始状态永远时 kConnecting
        // 因为创建 TcpConnection 时，socket fd 就是已经建立好连接的

        bool m_reading{ true };

        std::unique_ptr<Socket> m_socket;  // 客户端 socket
        std::unique_ptr<Channel> m_channel;  // 事件

        const InetAddress m_localAddr;  // 本机地址
        const InetAddress m_peerAddr;  // 远程地址
        
        ConnectionCallback m_connectionCallback;  // 连接回调
        MessageCallback m_messageCallback;  // 读事件回调
        WriteCompleteCallback m_writeCompleteCallback;  // 写完成回调
        CloseCallback m_closeCallback;  // 断线回调
        HighWaterMarkCallback m_highWaterMarkCallback;
        size_t m_highWaterMark{ 64 * 1024 * 1024 };

        Buffer m_inputBuffer;  // 输入缓冲区(socket 输入，recv 的信息)
        Buffer m_outputBuffer;  // 输出缓冲区(需要发送的数据，未发送的数据在此)
    };

    using TcpConnectionSPtr = std::shared_ptr<TcpConnection>;
}