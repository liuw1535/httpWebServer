#pragma once

/**
 * EventLoopThreadPool - 事件循环线程池
 * 实现一主多从的Reactor模型
 * MainReactor负责接受连接，SubReactor负责处理I/O
 */

#include <vector>
#include <memory>
#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>
#include "event_loop.h"
#include "logger.h"

namespace cppexpress {

class EventLoopThread {
public:
    EventLoopThread()
        : loop_(nullptr)
        , started_(false) {
    }

    ~EventLoopThread() {
        if (loop_) {
            loop_->quit();
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;

    EventLoop* startLoop() {
        thread_ = std::thread([this] { threadFunc(); });
        
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return loop_ != nullptr; });
        return loop_;
    }

private:
    void threadFunc() {
        EventLoop loop;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loop_ = &loop;
        }
        cond_.notify_one();
        loop.loop();
        
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = nullptr;
    }

    EventLoop* loop_;
    bool started_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

class EventLoopThreadPool {
public:
    EventLoopThreadPool(EventLoop* baseLoop, int numThreads)
        : baseLoop_(baseLoop)
        , numThreads_(numThreads)
        , started_(false)
        , next_(0) {
    }

    ~EventLoopThreadPool() = default;

    EventLoopThreadPool(const EventLoopThreadPool&) = delete;
    EventLoopThreadPool& operator=(const EventLoopThreadPool&) = delete;

    void start() {
        started_ = true;
        for (int i = 0; i < numThreads_; ++i) {
            auto thread = std::make_unique<EventLoopThread>();
            loops_.push_back(thread->startLoop());
            threads_.push_back(std::move(thread));
        }
        LOG_INFO("EventLoopThreadPool started with " << numThreads_ << " sub-reactors");
    }

    // 轮询选择下一个EventLoop（SubReactor）
    EventLoop* getNextLoop() {
        EventLoop* loop = baseLoop_;
        if (!loops_.empty()) {
            loop = loops_[next_];
            next_ = (next_ + 1) % static_cast<int>(loops_.size());
        }
        return loop;
    }

    std::vector<EventLoop*> getAllLoops() const {
        if (loops_.empty()) {
            return {baseLoop_};
        }
        return loops_;
    }

    bool started() const { return started_; }
    int numThreads() const { return numThreads_; }

private:
    EventLoop* baseLoop_;
    int numThreads_;
    bool started_;
    int next_;
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;
};

} // namespace cppexpress
