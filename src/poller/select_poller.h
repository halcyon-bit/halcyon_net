#pragma once

#include "../poller.h"

#include "platform.h"

#include <set>
#include <vector>

#ifdef WINDOWS
#include <WinSock2.h>
#elif defined LINUX
#include <sys/select.h>
#endif

namespace ephemeral::net
{
    /*
     * @brief   ::select
     */
    class SelectPoller : public Poller 
    {
    public:
        /*
         * @brief   构造函数
         */
        SelectPoller(EventLoop* loop);

        /*
         * @brief   析构函数
         */
        ~SelectPoller() override;

        /*
         * @brief       调用 select 获得当前活动的 IO 事件
         * @param[in]   超时时间
         * @param[out]  活动的 IO 事件
         */
        void poll(int timeoutMs, ChannelList* activeChannels) override;

        /*
         * @brief   更新 ChannelMap、m_pollfds 数组
         */
        void updateChannel(Channel* channel) override;

        /*
         * @brief   从 ChannelMap、m_pollfds 数组中移除 Channel
         */
        void removeChannel(Channel* channel) override;

    private:
        /*
         * @brief   查找活动的 Channel 并填充 activeChannels
         */
        void fillActiveChannels(int numEvents, ChannelList* activeChannels) const;
    
        /*
         * @brief   EventType -> select event
         */
        void setSelectEvent(int sockfd, int event);

    private:
        // select 事件类型
        enum FdSetType
        {
            FDSET_TYPE_READ = 0,  // 读
            FDSET_TYPE_WRITE = 1,  // 写
            FDSET_TYPE_EXCEPTION = 2,  // 异常
            FDSET_NUMBER = 3,
        };

        // for ::select()
        fd_set* m_fdset;  // 描述符集合
        fd_set* m_fdsetBackup;  // 备用描述符集合

        std::vector<int> m_selectfds;  // socket 集合
        std::set<int> m_sockfdSet;  // 用于获取当前最大的 socket，for ::select()
    };
}