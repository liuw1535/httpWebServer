#pragma once

/**
 * EventLoop - 事件循环
 * Reactor模式的核心，每个线程一个EventLoop
 * 负责I/O事件分发和定时器管理
 */

#include <functional>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include <thread>
#include "platform.h"
#include "epoller.h"
#include "channel.h"
#include "logger.h"

namespace cppexpress {

class EventLoop {
public:
    using Functor = std::function<void()>;

    EventLoop()
        : looping_(false)
        , quit_(false)
        , callingPendingFunctors_(false)
        , threadId_(std::this_thread::get_id())
        , epoller_(std::make_unique<Epoller>()) {
        // 创建唤醒机制
#ifdef PLATFORM_LINUX
        wakeupFd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (wakeupFd_ < 0) {
            LOG_FATAL("eventfd create failed");
        }
#else
        // Windows: 使用自连接的UDP socket对来模拟eventfd
        createWakeupPipe();
#endif
        wakeupChannel_ = std::make_unique<Channel>(this, wakeupFd_);
        wakeupChannel_->setReadCallback([this] { handleWakeup(); });
        wakeupChannel_->enableReading();
    }

    ~EventLoop() {
        wakeupChannel_->disableAll();
        wakeupChannel_->remove();
#ifdef PLATFORM_LINUX
        ::close(wakeupFd_);
#else
        SocketUtils::closeSocket(wakeupFd_);
        SocketUtils::closeSocket(wakeupPeerFd_);
#endif
    }

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // 开始事件循环
    void loop() {
        looping_ = true;
        quit_ = false;
        LOG_DEBUG("EventLoop " << this << " start looping");

        while (!quit_) {
            auto activeChannels = epoller_->poll(10000); // 10s超时
            for (auto* channel : activeChannels) {
                channel->handleEvent();
            }
            doPendingFunctors();
        }

        LOG_DEBUG("EventLoop " << this << " stop looping");
        looping_ = false;
    }

    // 退出事件循环
    void quit() {
        quit_ = true;
        if (!isInLoopThread()) {
            wakeup();
        }
    }

    // 在事件循环线程中执行
    void runInLoop(Functor cb) {
        if (isInLoopThread()) {
            cb();
        } else {
            queueInLoop(std::move(cb));
        }
    }

    // 加入待执行队列
    void queueInLoop(Functor cb) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pendingFunctors_.push_back(std::move(cb));
        }
        if (!isInLoopThread() || callingPendingFunctors_) {
            wakeup();
        }
    }

    // 唤醒事件循环
    void wakeup() {
#ifdef PLATFORM_LINUX
        uint64_t one = 1;
        ssize_t n = ::write(wakeupFd_, &one, sizeof(one));
        if (n != sizeof(one)) {
            LOG_ERROR("wakeup write " << n << " bytes");
        }
#else
        char one = 1;
        ::send(wakeupPeerFd_, &one, 1, 0);
#endif
    }

    // 更新Channel
    void updateChannel(Channel* channel) {
        epoller_->updateChannel(channel);
    }

    // 移除Channel
    void removeChannel(Channel* channel) {
        epoller_->removeChannel(channel);
    }

    // 是否在事件循环线程中
    bool isInLoopThread() const {
        return threadId_ == std::this_thread::get_id();
    }

    // 断言在事件循环线程中
    void assertInLoopThread() const {
        if (!isInLoopThread()) {
            LOG_FATAL("EventLoop was created in thread " << threadId_
                      << ", but current thread is " << std::this_thread::get_id());
        }
    }

private:
    void handleWakeup() {
#ifdef PLATFORM_LINUX
        uint64_t one = 0;
        ssize_t n = ::read(wakeupFd_, &one, sizeof(one));
        if (n != sizeof(one)) {
            LOG_ERROR("handleWakeup read " << n << " bytes");
        }
#else
        char buf[64];
        ::recv(wakeupFd_, buf, sizeof(buf), 0);
#endif
    }

    void doPendingFunctors() {
        std::vector<Functor> functors;
        callingPendingFunctors_ = true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            functors.swap(pendingFunctors_);
        }
        for (auto& functor : functors) {
            functor();
        }
        callingPendingFunctors_ = false;
    }

#ifndef PLATFORM_LINUX
    void createWakeupPipe() {
        // 使用本地UDP socket对模拟eventfd
        socket_t listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // 让系统分配端口

        ::bind(listener, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        ::listen(listener, 1);

        socklen_t addrLen = sizeof(addr);
        ::getsockname(listener, reinterpret_cast<struct sockaddr*>(&addr), &addrLen);

        wakeupPeerFd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ::connect(wakeupPeerFd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

        wakeupFd_ = ::accept(listener, nullptr, nullptr);
        SocketUtils::setNonBlocking(wakeupFd_);
        SocketUtils::setNonBlocking(wakeupPeerFd_);

        SocketUtils::closeSocket(listener);
    }

    socket_t wakeupPeerFd_;
#endif

    std::atomic<bool> looping_;
    std::atomic<bool> quit_;
    std::atomic<bool> callingPendingFunctors_;
    std::thread::id threadId_;

    std::unique_ptr<Epoller> epoller_;
    socket_t wakeupFd_;
    std::unique_ptr<Channel> wakeupChannel_;

    std::mutex mutex_;
    std::vector<Functor> pendingFunctors_;
};

// Channel方法的实现（依赖EventLoop）
inline void Channel::enableReading() {
    events_ = events_ | EventType::READ | EventType::ET;
    update();
}

inline void Channel::disableReading() {
    events_ = static_cast<EventType>(
        static_cast<uint32_t>(events_) & ~static_cast<uint32_t>(EventType::READ));
    update();
}

inline void Channel::enableWriting() {
    events_ = events_ | EventType::WRITE;
    update();
}

inline void Channel::disableWriting() {
    events_ = static_cast<EventType>(
        static_cast<uint32_t>(events_) & ~static_cast<uint32_t>(EventType::WRITE));
    update();
}

inline void Channel::disableAll() {
    events_ = EventType::NONE;
    update();
}

inline void Channel::remove() {
    loop_->removeChannel(this);
}

inline void Channel::update() {
    loop_->updateChannel(this);
}

} // namespace cppexpress
