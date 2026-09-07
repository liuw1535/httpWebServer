#pragma once

/**
 * HttpConnection - HTTP连接管理
 * 管理单个TCP连接上的HTTP请求/响应
 * 支持长连接（Keep-Alive）
 */

#include <memory>
#include <functional>
#include "platform.h"
#include "buffer.h"
#include "channel.h"
#include "event_loop.h"
#include "http_parser.h"
#include "http_response.h"
#include "logger.h"

namespace cppexpress {

class HttpConnection : public std::enable_shared_from_this<HttpConnection> {
public:
    using HttpCallback = std::function<void(HttpRequest&, HttpResponse&)>;
    using CloseCallback = std::function<void(const std::shared_ptr<HttpConnection>&)>;

    HttpConnection(EventLoop* loop, socket_t fd)
        : loop_(loop)
        , fd_(fd)
        , channel_(std::make_unique<Channel>(loop, fd))
        , state_(State::CONNECTED) {
        channel_->setReadCallback([this] { handleRead(); });
        channel_->setWriteCallback([this] { handleWrite(); });
        channel_->setCloseCallback([this] { handleClose(); });
        channel_->setErrorCallback([this] { handleError(); });

        SocketUtils::setNoDelay(fd);
        SocketUtils::setKeepAlive(fd);
    }

    ~HttpConnection() {
        SocketUtils::closeSocket(fd_);
    }

    HttpConnection(const HttpConnection&) = delete;
    HttpConnection& operator=(const HttpConnection&) = delete;

    // 启动连接（开始监听读事件）
    void start() {
        auto self = shared_from_this();
        channel_->tie(self);
        channel_->enableReading();
        LOG_DEBUG("Connection established, fd=" << fd_);
    }

    // 关闭连接
    void shutdown() {
        if (state_ == State::CONNECTED) {
            state_ = State::DISCONNECTING;
            loop_->runInLoop([self = shared_from_this()] {
                if (self->channel_->isWriting()) {
                    // 等待写完再关闭
                } else {
                    self->handleClose();
                }
            });
        }
    }

    // 强制关闭
    void forceClose() {
        if (state_ == State::CONNECTED || state_ == State::DISCONNECTING) {
            state_ = State::DISCONNECTING;
            loop_->queueInLoop([self = shared_from_this()] {
                self->handleClose();
            });
        }
    }

    // 设置回调
    void setHttpCallback(HttpCallback cb) { httpCallback_ = std::move(cb); }
    void setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }

    socket_t fd() const { return fd_; }
    EventLoop* getLoop() const { return loop_; }
    bool connected() const { return state_ == State::CONNECTED; }

private:
    enum class State {
        CONNECTING,
        CONNECTED,
        DISCONNECTING,
        DISCONNECTED
    };

    void handleRead() {
        int savedErrno = 0;
        ssize_t n = inputBuffer_.readFd(fd_, &savedErrno);

        if (n > 0) {
            // 解析HTTP请求（支持长连接，可能有多个请求）
            processRequests();
        } else if (n == 0) {
            // 对端关闭
            handleClose();
        } else {
            // 读取错误
            LOG_ERROR("read error, fd=" << fd_ << " errno=" << savedErrno);
            handleError();
        }
    }

    void processRequests() {
        while (inputBuffer_.readableBytes() > 0) {
            HttpRequest request;
            auto result = parser_.parse(inputBuffer_, request);

            if (result == HttpParser::ParseResult::ERROR) {
                LOG_ERROR("HTTP parse error, fd=" << fd_);
                // 发送400错误
                HttpResponse response(HttpVersion::HTTP_11, false);
                response.status(400).send("Bad Request");
                sendResponse(response);
                handleClose();
                return;
            }

            if (result == HttpParser::ParseResult::NEED_MORE) {
                // 需要更多数据，等待下次读取
                return;
            }

            // 解析完成，处理请求
            if (result == HttpParser::ParseResult::COMPLETE) {
                bool keepAlive = request.keepAlive();
                HttpResponse response(request.version(), keepAlive);

                if (httpCallback_) {
                    httpCallback_(request, response);
                }

                // 如果处理函数没有发送响应，发送默认404
                if (!response.isSent()) {
                    response.status(404).send("Not Found");
                }

                sendResponse(response);

                // 重置解析器以处理下一个请求（长连接）
                parser_.reset();

                if (!keepAlive) {
                    shutdown();
                    return;
                }
            }
        }
    }

    void sendResponse(const HttpResponse& response) {
        std::string data = response.build();
        outputBuffer_.append(data);

        if (!channel_->isWriting()) {
            handleWrite();
        }
    }

    void handleWrite() {
        if (outputBuffer_.readableBytes() > 0) {
            int savedErrno = 0;
            ssize_t n = outputBuffer_.writeFd(fd_, &savedErrno);
            if (n > 0) {
                outputBuffer_.retrieve(n);
                if (outputBuffer_.readableBytes() == 0) {
                    channel_->disableWriting();
                    if (state_ == State::DISCONNECTING) {
                        handleClose();
                    }
                } else {
                    // 还有数据要写，继续关注写事件
                    if (!channel_->isWriting()) {
                        channel_->enableWriting();
                    }
                }
            } else {
                LOG_ERROR("write error, fd=" << fd_);
            }
        }
    }

    void handleClose() {
        if (state_ == State::DISCONNECTED) return;
        state_ = State::DISCONNECTED;
        channel_->disableAll();
        channel_->remove();
        LOG_DEBUG("Connection closed, fd=" << fd_);

        if (closeCallback_) {
            closeCallback_(shared_from_this());
        }
    }

    void handleError() {
        int err = SocketUtils::getSocketError(fd_);
        LOG_ERROR("Connection error, fd=" << fd_ << " error=" << err);
        handleClose();
    }

    EventLoop* loop_;
    socket_t fd_;
    std::unique_ptr<Channel> channel_;
    State state_;

    Buffer inputBuffer_;
    Buffer outputBuffer_;
    HttpParser parser_;

    HttpCallback httpCallback_;
    CloseCallback closeCallback_;
};

} // namespace cppexpress
