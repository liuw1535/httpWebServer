#pragma once

/**
 * Channel - 事件通道
 * 封装文件描述符及其关注的事件和回调函数
 * 是Reactor模式中的核心组件
 */

#include <functional>
#include <memory>
#include "platform.h"

namespace cppexpress {

class EventLoop;

class Channel {
public:
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, socket_t fd)
        : loop_(loop)
        , fd_(fd)
        , events_(EventType::NONE)
        , revents_(EventType::NONE)
        , index_(-1)
        , tied_(false) {
    }

    ~Channel() = default;

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // 处理事件
    void handleEvent() {
        if (tied_) {
            auto guard = tie_.lock();
            if (guard) {
                handleEventWithGuard();
            }
        } else {
            handleEventWithGuard();
        }
    }

    // 绑定生命周期（防止回调时对象已销毁）
    void tie(const std::shared_ptr<void>& obj) {
        tie_ = obj;
        tied_ = true;
    }

    // 设置回调
    void setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 获取/设置属性
    socket_t fd() const { return fd_; }
    EventType events() const { return events_; }
    void setRevents(EventType revt) { revents_ = revt; }
    int index() const { return index_; }
    void setIndex(int idx) { index_ = idx; }
    EventLoop* ownerLoop() const { return loop_; }

    // 是否没有关注任何事件
    bool isNoneEvent() const { return events_ == EventType::NONE; }
    bool isWriting() const { return hasEvent(events_, EventType::WRITE); }
    bool isReading() const { return hasEvent(events_, EventType::READ); }

    // 启用/禁用事件
    void enableReading();
    void disableReading();
    void enableWriting();
    void disableWriting();
    void disableAll();

    // 从EventLoop中移除
    void remove();

private:
    void handleEventWithGuard() {
        // 关闭事件
        if (hasEvent(revents_, EventType::CLOSE) && !hasEvent(revents_, EventType::READ)) {
            if (closeCallback_) closeCallback_();
        }
        // 错误事件
        if (hasEvent(revents_, EventType::ERR)) {
            if (errorCallback_) errorCallback_();
        }
        // 可读事件
        if (hasEvent(revents_, EventType::READ)) {
            if (readCallback_) readCallback_();
        }
        // 可写事件
        if (hasEvent(revents_, EventType::WRITE)) {
            if (writeCallback_) writeCallback_();
        }
    }

    void update();

    EventLoop* loop_;
    socket_t fd_;
    EventType events_;
    EventType revents_;
    int index_;  // 在Epoller中的状态: -1=新的, 1=已添加, 2=已删除

    std::weak_ptr<void> tie_;
    bool tied_;

    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};

} // namespace cppexpress
