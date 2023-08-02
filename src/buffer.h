#pragma once

#include <vector>
#include <string>
#include <cassert>
#include <string_view>

namespace ephemeral::net
{
    /// +-------------------+------------------+------------------+
    /// | prependable bytes |  readable bytes  |  writable bytes  |
    /// |                   |     (CONTENT)    |                  |
    /// +-------------------+------------------+------------------+
    /// |                   |                  |                  |
    /// 0      <=      readerIndex   <=   writerIndex    <=     size

    class Buffer
    {
    public:
        static constexpr size_t initialSize = 1024;
        static constexpr size_t initialPerpend = 8;

    public:
        Buffer()
            : m_buffer(initialPerpend + initialSize)
            , m_readerIndex(initialPerpend)
            , m_writerIndex(initialPerpend)
        {

        }

        void swap(Buffer& rhs)
        {
            m_buffer.swap(rhs.m_buffer);
            std::swap(m_readerIndex, rhs.m_readerIndex);
            std::swap(m_writerIndex, rhs.m_writerIndex);
        }

        /*
         * @brief   可读数据的大小
         */
        size_t readableBytes() const
        {
            return m_writerIndex - m_readerIndex;
        }

        /*
         * @brief   buffer 中当前可写入的大小
         */
        size_t writableBytes() const
        {
            return m_buffer.size() - m_writerIndex;
        }

        /*
         * @brief   buffer 中预留空间的大小
         */
        size_t prependableBytes() const
        {
            return m_readerIndex;
        }

        /*
         * @brief   获取可读数据的起始位置
         */
        const char* peek() const
        {
            return begin() + m_readerIndex;
        }

        // 待定
        void retrieve(size_t len)
        {
            assert(len <= readableBytes());
            m_readerIndex += len;
        }
        // 待定
        void retrieveUntil(const char* end)
        {

        }

        /*
         * @brief   重置 Buffer(即清空数据)
         */
        void reset()
        {
            m_readerIndex = initialPerpend;
            m_writerIndex = initialPerpend;
        }

        /*
         * @brief   取出 Buffer 中的所有数据
         * @return  数据
         */
        std::string retrieveAsString()
        {
            std::string str(peek(), readableBytes());
            reset();
            return str;
        }

        /*
         * @brief   向 Buffer 中添加数据
         */
        void append(const std::string_view& str)
        {
            append(str.data(), str.length());
        }

        /*
         * @brief   向 Buffer 中添加数据
         */
        void append(const char* data, size_t len)
        {
            ensureWritableBytes(len);
            std::copy(data, data + len, beginWrite());
            hasWritten(len);
        }

        /*
         * @brief   向 Buffer 中添加数据
         */
        void append(const void* data, size_t len)
        {
            append(static_cast<const char*>(data), len);
        }

        /*
         * @brief   确保 Buffer 中可写入的空间 >= len，不足会扩容
         */
        void ensureWritableBytes(size_t len)
        {
            if (writableBytes() < len) {
                resize(len);
            }
            assert(writableBytes() >= len);
        }

        /*
         * @brief   获取可写入空间的起始位置
         */
        char* beginWrite()
        {
            return begin() + m_writerIndex;
        }
        const char* beginWrite() const
        {
            return begin() + m_writerIndex;
        }

        /*
         * @brief       调整可写位置(并未写入数据，仅调整位置0
         * @param[in]   已写入的字节
         */
        void hasWritten(size_t len)
        {
            m_writerIndex += len;
        }

        /*
         * @brief   向缓冲区头部写入数据
         * @ps      外部需要确保 len <= prependableBytes()，防止覆盖正常数据
         */
        void prepend(const void* data, size_t len)
        {
            assert(len <= prependableBytes());
            m_readerIndex -= len;
            const char* val = static_cast<const char*>(data);
            std::copy(val, val + len, begin() + m_readerIndex);
        }

        /*
         * @brief   调整缓冲区的大小(一般用于缩小缓冲区)
         */
        void shrink(size_t reserve)
        {
            std::vector<char> buf(initialPerpend + readableBytes() + reserve);
            std::copy(peek(), peek() + readableBytes(), buf.begin() + initialPerpend);
            buf.swap(m_buffer);
        }

        /*
         * @brief   读取 socket 中的数据
         * @return  读取的长度
         */
        int readFd(int fd, int* savedError);

    private:
        /*
         * @brief   获取 m_buffer 的起始位置
         */
        char* begin()
        {
            return &*m_buffer.begin();
        }
        const char* begin() const
        {
            return &*m_buffer.cbegin();
        }

        /*
         * @brief   扩容
         * @param[in]
         * @param[out]
         * @return
         */
        void resize(size_t len)
        {
            if (writableBytes() + prependableBytes() < len + initialPerpend) {
                // 扩容
                m_buffer.resize(m_writerIndex + len);
            }
            else {
                // 如果预留空间+可写空间满足 len，则将当前 Buffer 中的数据向前移动
                assert(initialPerpend < m_readerIndex);
                size_t readable = readableBytes();
                std::copy(begin() + m_readerIndex, begin() + m_writerIndex, begin() + initialPerpend);
                // 修正位置
                m_readerIndex = initialPerpend;
                m_writerIndex = m_readerIndex + readable;
                assert(readable == readableBytes());
            }
        }

    private:
        std::vector<char> m_buffer;  // 缓冲区
        // 不使用 m_buffer 中的地址，而使用 size_t，是因为 m_buffer 扩容时，会重新
        // 分配内存，会导致原内存失效
        size_t m_readerIndex;  // 可读的位置
        size_t m_writerIndex;  // 可写的位置
    };
}