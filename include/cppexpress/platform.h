#pragma once

/**
 * 平台抽象层 - 统一跨平台API
 * 在Linux上使用epoll，在Windows上使用select/IOCP模拟
 */

#include <cstdint>
#include <string>
#include <functional>
#include <vector>
#include <cstring>

// Windows头文件中的宏冲突处理
#ifdef PLATFORM_WINDOWS
    // 必须在包含windows头文件前取消可能冲突的宏
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    // 取消Windows定义的冲突宏
    #ifdef ERROR
    #undef ERROR
    #endif
    #ifdef DELETE
    #undef DELETE
    #endif
    #ifdef min
    #undef min
    #endif
    #ifdef max
    #undef max
    #endif
    using socket_t = SOCKET;
    constexpr socket_t INVALID_SOCKET_VAL = INVALID_SOCKET;
    #define CLOSE_SOCKET(s) closesocket(s)
#else
    #include <sys/socket.h>
    #include <sys/epoll.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <netinet/tcp.h>
    #include <sys/eventfd.h>
    #include <signal.h>
    using socket_t = int;
    constexpr socket_t INVALID_SOCKET_VAL = -1;
    #define CLOSE_SOCKET(s) ::close(s)
#endif

namespace cppexpress {

// 事件类型
enum class EventType : uint32_t {
    NONE    = 0,
    READ    = 1 << 0,
    WRITE   = 1 << 1,
    ERR     = 1 << 2,
    CLOSE   = 1 << 3,
    ET      = 1 << 4,  // 边缘触发
};

inline EventType operator|(EventType a, EventType b) {
    return static_cast<EventType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline EventType operator&(EventType a, EventType b) {
    return static_cast<EventType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool hasEvent(EventType events, EventType flag) {
    return (static_cast<uint32_t>(events) & static_cast<uint32_t>(flag)) != 0;
}

/**
 * 平台初始化/清理
 */
class PlatformInit {
public:
    PlatformInit() {
#ifdef PLATFORM_WINDOWS
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#else
        signal(SIGPIPE, SIG_IGN);
#endif
    }
    ~PlatformInit() {
#ifdef PLATFORM_WINDOWS
        WSACleanup();
#endif
    }
    PlatformInit(const PlatformInit&) = delete;
    PlatformInit& operator=(const PlatformInit&) = delete;
};

/**
 * Socket工具函数
 */
namespace SocketUtils {

inline socket_t createTcpSocket() {
    socket_t fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    return fd;
}

inline bool setNonBlocking(socket_t fd) {
#ifdef PLATFORM_WINDOWS
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

inline bool setReuseAddr(socket_t fd) {
    int opt = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                      reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
}

inline bool setReusePort(socket_t fd) {
#ifdef PLATFORM_LINUX
    int opt = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEPORT,
                      reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
#else
    (void)fd;
    return true;
#endif
}

inline bool setNoDelay(socket_t fd) {
    int opt = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                      reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
}

inline bool setKeepAlive(socket_t fd) {
    int opt = 1;
    return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
                      reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
}

inline bool bindAndListen(socket_t fd, const std::string& ip, uint16_t port, int backlog = 1024) {
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (ip.empty() || ip == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    }
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        return false;
    }
    if (::listen(fd, backlog) != 0) {
        return false;
    }
    return true;
}

inline socket_t acceptConnection(socket_t listenFd, struct sockaddr_in* clientAddr) {
    socklen_t len = sizeof(struct sockaddr_in);
    socket_t clientFd = ::accept(listenFd, reinterpret_cast<struct sockaddr*>(clientAddr), &len);
    return clientFd;
}

inline void closeSocket(socket_t fd) {
    if (fd != INVALID_SOCKET_VAL) {
        CLOSE_SOCKET(fd);
    }
}

inline int getSocketError(socket_t fd) {
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(fd, SOL_SOCKET, SO_ERROR,
               reinterpret_cast<char*>(&error), &len);
    return error;
}

} // namespace SocketUtils

} // namespace cppexpress
