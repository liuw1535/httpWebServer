#pragma once

/**
 * Epoller - I/O多路复用器
 * Linux上使用epoll（边缘触发），其他平台使用select模拟
 */

#include <vector>
#include <unordered_map>
#include "platform.h"
#include "channel.h"
#include "logger.h"

#ifdef PLATFORM_LINUX
#include <sys/epoll.h>
#endif

namespace cppexpress {

class Epoller {
public:
    Epoller()
#ifdef PLATFORM_LINUX
        : epollFd_(::epoll_create1(EPOLL_CLOEXEC))
        , events_(16)
#endif
    {
#ifdef PLATFORM_LINUX
        if (epollFd_ < 0) {
            LOG_FATAL("epoll_create1 failed");
        }
#endif
    }

    ~Epoller() {
#ifdef PLATFORM_LINUX
        ::close(epollFd_);
#endif
    }

    Epoller(const Epoller&) = delete;
    Epoller& operator=(const Epoller&) = delete;

    // 等待事件，返回活跃的Channel列表
    std::vector<Channel*> poll(int timeoutMs = -1) {
        std::vector<Channel*> activeChannels;

#ifdef PLATFORM_LINUX
        int numEvents = ::epoll_wait(epollFd_, events_.data(),
                                      static_cast<int>(events_.size()), timeoutMs);
        if (numEvents > 0) {
            for (int i = 0; i < numEvents; ++i) {
                Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
                EventType revents = EventType::NONE;

                if (events_[i].events & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
                    revents = revents | EventType::READ;
                }
                if (events_[i].events & EPOLLOUT) {
                    revents = revents | EventType::WRITE;
                }
                if (events_[i].events & EPOLLERR) {
                    revents = revents | EventType::ERR;
                }
                if (events_[i].events & EPOLLHUP) {
                    revents = revents | EventType::CLOSE;
                }

                channel->setRevents(revents);
                activeChannels.push_back(channel);
            }
            // 如果所有事件槽都用满了，扩容
            if (static_cast<size_t>(numEvents) == events_.size()) {
                events_.resize(events_.size() * 2);
            }
        } else if (numEvents < 0) {
            if (errno != EINTR) {
                LOG_ERROR("epoll_wait error: " << strerror(errno));
            }
        }
#else
        // 跨平台select实现
        fd_set readfds, writefds, exceptfds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);

        socket_t maxfd = INVALID_SOCKET_VAL;

        for (auto& [fd, channel] : channels_) {
            if (hasEvent(channel->events(), EventType::READ)) {
                FD_SET(fd, &readfds);
            }
            if (hasEvent(channel->events(), EventType::WRITE)) {
                FD_SET(fd, &writefds);
            }
            FD_SET(fd, &exceptfds);
#ifdef PLATFORM_WINDOWS
            (void)maxfd; // Windows select ignores maxfd
#else
            if (fd > maxfd || maxfd == INVALID_SOCKET_VAL) maxfd = fd;
#endif
        }

        struct timeval tv;
        struct timeval* tvp = nullptr;
        if (timeoutMs >= 0) {
            tv.tv_sec = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            tvp = &tv;
        }

        int ret = ::select(static_cast<int>(maxfd + 1), &readfds, &writefds, &exceptfds, tvp);
        if (ret > 0) {
            for (auto& [fd, channel] : channels_) {
                EventType revents = EventType::NONE;
                if (FD_ISSET(fd, &readfds)) {
                    revents = revents | EventType::READ;
                }
                if (FD_ISSET(fd, &writefds)) {
                    revents = revents | EventType::WRITE;
                }
                if (FD_ISSET(fd, &exceptfds)) {
                    revents = revents | EventType::ERR;
                }
                if (revents != EventType::NONE) {
                    channel->setRevents(revents);
                    activeChannels.push_back(channel);
                }
            }
        }
#endif
        return activeChannels;
    }

    // 更新Channel的关注事件
    void updateChannel(Channel* channel) {
        int index = channel->index();

#ifdef PLATFORM_LINUX
        if (index == -1 || index == 2) {
            // 新Channel或已删除的Channel，添加到epoll
            struct epoll_event ev;
            std::memset(&ev, 0, sizeof(ev));
            ev.events = toEpollEvents(channel->events());
            ev.data.ptr = channel;

            if (::epoll_ctl(epollFd_, EPOLL_CTL_ADD, channel->fd(), &ev) < 0) {
                LOG_ERROR("epoll_ctl ADD failed for fd " << channel->fd());
                return;
            }
            channel->setIndex(1);
        } else {
            // 已存在的Channel，修改事件
            if (channel->isNoneEvent()) {
                struct epoll_event ev;
                std::memset(&ev, 0, sizeof(ev));
                if (::epoll_ctl(epollFd_, EPOLL_CTL_DEL, channel->fd(), &ev) < 0) {
                    LOG_ERROR("epoll_ctl DEL failed for fd " << channel->fd());
                }
                channel->setIndex(2);
            } else {
                struct epoll_event ev;
                std::memset(&ev, 0, sizeof(ev));
                ev.events = toEpollEvents(channel->events());
                ev.data.ptr = channel;
                if (::epoll_ctl(epollFd_, EPOLL_CTL_MOD, channel->fd(), &ev) < 0) {
                    LOG_ERROR("epoll_ctl MOD failed for fd " << channel->fd());
                }
            }
        }
#else
        (void)index;
        channels_[channel->fd()] = channel;
        channel->setIndex(1);
#endif
    }

    // 移除Channel
    void removeChannel(Channel* channel) {
#ifdef PLATFORM_LINUX
        if (channel->index() == 1) {
            struct epoll_event ev;
            std::memset(&ev, 0, sizeof(ev));
            ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, channel->fd(), &ev);
        }
#else
        channels_.erase(channel->fd());
#endif
        channel->setIndex(-1);
    }

private:
#ifdef PLATFORM_LINUX
    static uint32_t toEpollEvents(EventType events) {
        uint32_t epollEvents = 0;
        if (hasEvent(events, EventType::READ)) {
            epollEvents |= EPOLLIN | EPOLLPRI;
        }
        if (hasEvent(events, EventType::WRITE)) {
            epollEvents |= EPOLLOUT;
        }
        if (hasEvent(events, EventType::ET)) {
            epollEvents |= EPOLLET;
        }
        epollEvents |= EPOLLRDHUP;
        return epollEvents;
    }

    int epollFd_;
    std::vector<struct epoll_event> events_;
#else
    std::unordered_map<socket_t, Channel*> channels_;
#endif
};

} // namespace cppexpress
