#pragma once

/**
 * 高效缓冲区 - 用于网络I/O读写
 * 参考muduo的Buffer设计，支持自动扩容
 * 
 * +-------------------+------------------+------------------+
 * | prependable bytes |  readable bytes  |  writable bytes  |
 * |                   |     (CONTENT)    |                  |
 * +-------------------+------------------+------------------+
 * |                   |                  |                  |
 * 0      <=      readerIndex   <=   writerIndex    <=     size
 */

#include <vector>
#include <string>
#include <algorithm>
#include <cassert>
#include <cstring>
#include "platform.h"

namespace cppexpress {

class Buffer {
public:
    static constexpr size_t kPrependSize = 8;
    static constexpr size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kPrependSize + initialSize)
        , readerIndex_(kPrependSize)
        , writerIndex_(kPrependSize) {
    }

    // 可读字节数
    size_t readableBytes() const { return writerIndex_ - readerIndex_; }

    // 可写字节数
    size_t writableBytes() const { return buffer_.size() - writerIndex_; }

    // 前置空间
    size_t prependableBytes() const { return readerIndex_; }

    // 可读数据起始指针
    const char* peek() const { return begin() + readerIndex_; }

    // 查找CRLF
    const char* findCRLF() const {
        const char* crlf = std::search(peek(), beginWrite(), kCRLF, kCRLF + 2);
        return crlf == beginWrite() ? nullptr : crlf;
    }

    const char* findCRLF(const char* start) const {
        assert(peek() <= start);
        assert(start <= beginWrite());
        const char* crlf = std::search(start, beginWrite(), kCRLF, kCRLF + 2);
        return crlf == beginWrite() ? nullptr : crlf;
    }

    // 查找换行符
    const char* findEOL() const {
        const void* eol = std::memchr(peek(), '\n', readableBytes());
        return static_cast<const char*>(eol);
    }

    // 取回数据（移动读指针）
    void retrieve(size_t len) {
        assert(len <= readableBytes());
        if (len < readableBytes()) {
            readerIndex_ += len;
        } else {
            retrieveAll();
        }
    }

    void retrieveUntil(const char* end) {
        assert(peek() <= end);
        assert(end <= beginWrite());
        retrieve(end - peek());
    }

    void retrieveAll() {
        readerIndex_ = kPrependSize;
        writerIndex_ = kPrependSize;
    }

    // 取回所有数据为string
    std::string retrieveAllAsString() {
        return retrieveAsString(readableBytes());
    }

    std::string retrieveAsString(size_t len) {
        assert(len <= readableBytes());
        std::string result(peek(), len);
        retrieve(len);
        return result;
    }

    // 追加数据
    void append(const std::string& str) {
        append(str.data(), str.size());
    }

    void append(const char* data, size_t len) {
        ensureWritableBytes(len);
        std::copy(data, data + len, beginWrite());
        hasWritten(len);
    }

    void append(const void* data, size_t len) {
        append(static_cast<const char*>(data), len);
    }

    // 确保有足够的可写空间
    void ensureWritableBytes(size_t len) {
        if (writableBytes() < len) {
            makeSpace(len);
        }
        assert(writableBytes() >= len);
    }

    char* beginWrite() { return begin() + writerIndex_; }
    const char* beginWrite() const { return begin() + writerIndex_; }

    void hasWritten(size_t len) {
        assert(len <= writableBytes());
        writerIndex_ += len;
    }

    void unwrite(size_t len) {
        assert(len <= readableBytes());
        writerIndex_ -= len;
    }

    // 前置数据
    void prepend(const void* data, size_t len) {
        assert(len <= prependableBytes());
        readerIndex_ -= len;
        const char* d = static_cast<const char*>(data);
        std::copy(d, d + len, begin() + readerIndex_);
    }

    // 收缩缓冲区
    void shrink(size_t reserve) {
        Buffer other;
        other.ensureWritableBytes(readableBytes() + reserve);
        other.append(peek(), readableBytes());
        swap(other);
    }

    size_t capacity() const { return buffer_.capacity(); }

    // 从fd读取数据（使用readv/scatter read提高效率）
    ssize_t readFd(socket_t fd, int* savedErrno) {
        char extrabuf[65536];
        const size_t writable = writableBytes();

#ifdef PLATFORM_LINUX
        struct iovec vec[2];
        vec[0].iov_base = beginWrite();
        vec[0].iov_len = writable;
        vec[1].iov_base = extrabuf;
        vec[1].iov_len = sizeof(extrabuf);

        const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
        const ssize_t n = ::readv(fd, vec, iovcnt);
        if (n < 0) {
            *savedErrno = errno;
        } else if (static_cast<size_t>(n) <= writable) {
            writerIndex_ += n;
        } else {
            writerIndex_ = buffer_.size();
            append(extrabuf, n - writable);
        }
#else
        // Windows: 使用recv
        ensureWritableBytes(writable > 0 ? writable : sizeof(extrabuf));
        const ssize_t n = ::recv(fd, beginWrite(),
                                 static_cast<int>(writableBytes()), 0);
        if (n < 0) {
#ifdef PLATFORM_WINDOWS
            *savedErrno = WSAGetLastError();
#else
            *savedErrno = errno;
#endif
        } else {
            writerIndex_ += n;
        }
#endif
        return n;
    }

    // 向fd写入数据
    ssize_t writeFd(socket_t fd, int* savedErrno) {
        ssize_t n = ::send(fd, peek(),
                          static_cast<int>(readableBytes()), 0);
        if (n < 0) {
#ifdef PLATFORM_WINDOWS
            *savedErrno = WSAGetLastError();
#else
            *savedErrno = errno;
#endif
        }
        return n;
    }

    void swap(Buffer& rhs) {
        buffer_.swap(rhs.buffer_);
        std::swap(readerIndex_, rhs.readerIndex_);
        std::swap(writerIndex_, rhs.writerIndex_);
    }

private:
    char* begin() { return buffer_.data(); }
    const char* begin() const { return buffer_.data(); }

    void makeSpace(size_t len) {
        if (writableBytes() + prependableBytes() < len + kPrependSize) {
            // 需要扩容
            buffer_.resize(writerIndex_ + len);
        } else {
            // 移动数据到前面
            size_t readable = readableBytes();
            std::copy(begin() + readerIndex_, begin() + writerIndex_,
                     begin() + kPrependSize);
            readerIndex_ = kPrependSize;
            writerIndex_ = readerIndex_ + readable;
        }
    }

    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;

    static constexpr char kCRLF[] = "\r\n";
};

} // namespace cppexpress
