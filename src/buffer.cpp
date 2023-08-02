#include "buffer.h"

#include "platform.h"
#include "base/types.h"

#ifdef WINDOWS
#include "sockets_ops.h"
#elif defined LINUX
#include <sys/uio.h>
#endif

using namespace ephemeral::net;
#ifdef WINDOWS
int Buffer::readFd(int fd, int* savedErroe)
{
    int bytes = sockets::getReadBytesOfSocket(fd);
    if (bytes <= 0) {
        return bytes;
    }

    char extrabuf[65535];
    while (1) {
        if (bytes > sizeof(extrabuf)) {
            bytes = sizeof(extrabuf);
        }
        int n = sockets::read(fd, extrabuf, bytes);
        if (n <= 0) {
            return n;
        }
        else {
            append(extrabuf, n);
        }

        bytes = sockets::getReadBytesOfSocket(fd);
        if (bytes < 0) {
            return bytes;
        }
        else if (bytes == 0) {
            return n;
        }
    }
}
#elif defined LINUX
int Buffer::readFd(int fd, int* saveErrno)
{
    char extrabuf[65535];
    struct iovec vec[2];
    const size_t writable = writableBytes();
    vec[0].iov_base = begin() + m_writerIndex;
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);
    const int n = readv(fd, vec, 2);
    if (n < 0) {
        *saveErrno = errno;
    }
    else if (base::implicit_cast<size_t>(n) <= writable) {
        m_writerIndex += n;
    }
    else {
        m_writerIndex = m_buffer.size();
        append(extrabuf, n - writable);
    }
    return n;
}
#endif