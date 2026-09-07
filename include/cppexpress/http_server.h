#pragma once

/**
 * HttpServer - Express风格的HTTP服务器主类
 * 整合Reactor模型、路由系统、中间件等所有组件
 * 
 * 使用方式类似Node.js Express:
 *   CppExpress app;
 *   app.get("/", [](auto& req, auto& res) { res.send("Hello!"); });
 *   app.listen(3000);
 */

#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include <atomic>
#include "platform.h"
#include "event_loop.h"
#include "event_loop_thread_pool.h"
#include "channel.h"
#include "http_connection.h"
#include "router.h"
#include "static_files.h"
#include "logger.h"

namespace cppexpress {

class HttpServer {
public:
    explicit HttpServer(int numThreads = 4)
        : numThreads_(numThreads)
        , started_(false)
        , nextConnId_(0)
        , listenFd_(INVALID_SOCKET_VAL)
        , router_(std::make_shared<Router>()) {
    }

    ~HttpServer() {
        stop();
    }

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // ============ Express风格的API ============

    // GET路由
    HttpServer& get(const std::string& path, HandlerFunction handler) {
        router_->get(path, std::move(handler));
        return *this;
    }

    // POST路由
    HttpServer& post(const std::string& path, HandlerFunction handler) {
        router_->post(path, std::move(handler));
        return *this;
    }

    // PUT路由
    HttpServer& put(const std::string& path, HandlerFunction handler) {
        router_->put(path, std::move(handler));
        return *this;
    }

    // DELETE路由
    HttpServer& del(const std::string& path, HandlerFunction handler) {
        router_->del(path, std::move(handler));
        return *this;
    }

    // PATCH路由
    HttpServer& patch(const std::string& path, HandlerFunction handler) {
        router_->patch(path, std::move(handler));
        return *this;
    }

    // HEAD路由
    HttpServer& head(const std::string& path, HandlerFunction handler) {
        router_->head(path, std::move(handler));
        return *this;
    }

    // OPTIONS路由
    HttpServer& options(const std::string& path, HandlerFunction handler) {
        router_->options(path, std::move(handler));
        return *this;
    }

    // 所有方法
    HttpServer& all(const std::string& path, HandlerFunction handler) {
        router_->all(path, std::move(handler));
        return *this;
    }

    // 添加中间件
    HttpServer& use(MiddlewareFunction middleware) {
        router_->use(std::move(middleware));
        return *this;
    }

    // 添加路径中间件
    HttpServer& use(const std::string& path, MiddlewareFunction middleware) {
        router_->use(path, std::move(middleware));
        return *this;
    }

    // 挂载子路由器
    HttpServer& use(const std::string& prefix, std::shared_ptr<Router> subRouter) {
        router_->use(prefix, std::move(subRouter));
        return *this;
    }

    // 静态文件服务
    HttpServer& serveStatic(const std::string& root, const std::string& prefix = "") {
        router_->use(middleware::staticFiles(root, prefix));
        return *this;
    }

    // 设置日志级别
    HttpServer& setLogLevel(LogLevel level) {
        Logger::instance().setLevel(level);
        return *this;
    }

    // ============ 服务器控制 ============

    /**
     * 启动服务器监听
     * @param port 端口号
     * @param host 绑定地址，默认0.0.0.0
     * @param callback 启动成功回调
     */
    void listen(uint16_t port, const std::string& host = "0.0.0.0",
                std::function<void()> callback = nullptr) {
        port_ = port;
        host_ = host;

        // 初始化平台
        static PlatformInit platformInit;

        // 创建监听socket
        listenFd_ = SocketUtils::createTcpSocket();
        if (listenFd_ == INVALID_SOCKET_VAL) {
            LOG_FATAL("Failed to create listen socket");
            return;
        }

        SocketUtils::setReuseAddr(listenFd_);
        SocketUtils::setReusePort(listenFd_);
        SocketUtils::setNonBlocking(listenFd_);

        if (!SocketUtils::bindAndListen(listenFd_, host, port)) {
            LOG_FATAL("Failed to bind and listen on " << host << ":" << port);
            SocketUtils::closeSocket(listenFd_);
            return;
        }

        // 创建主EventLoop
        mainLoop_ = std::make_unique<EventLoop>();

        // 创建SubReactor线程池
        threadPool_ = std::make_unique<EventLoopThreadPool>(mainLoop_.get(), numThreads_);
        threadPool_->start();

        // 创建监听Channel
        listenChannel_ = std::make_unique<Channel>(mainLoop_.get(), listenFd_);
        listenChannel_->setReadCallback([this] { handleNewConnection(); });
        listenChannel_->enableReading();

        started_ = true;

        if (callback) {
            callback();
        }

        LOG_INFO("CppExpress server listening on http://" << host << ":" << port);
        LOG_INFO("Using " << numThreads_ << " worker threads");

        // 进入事件循环（阻塞）
        mainLoop_->loop();
    }

    /**
     * 停止服务器
     */
    void stop() {
        if (!started_) return;
        started_ = false;

        if (mainLoop_) {
            mainLoop_->quit();
        }

        // 关闭所有连接
        for (auto& [id, conn] : connections_) {
            conn->forceClose();
        }
        connections_.clear();

        // 关闭监听socket
        if (listenFd_ != INVALID_SOCKET_VAL) {
            SocketUtils::closeSocket(listenFd_);
            listenFd_ = INVALID_SOCKET_VAL;
        }

        LOG_INFO("Server stopped");
    }

private:
    /**
     * 处理新连接 - MainReactor的职责
     */
    void handleNewConnection() {
        struct sockaddr_in clientAddr;
        socket_t connFd = SocketUtils::acceptConnection(listenFd_, &clientAddr);

        while (connFd != INVALID_SOCKET_VAL) {
            SocketUtils::setNonBlocking(connFd);

            // 轮询选择一个SubReactor
            EventLoop* ioLoop = threadPool_->getNextLoop();
            
            int connId = nextConnId_++;
            auto conn = std::make_shared<HttpConnection>(ioLoop, connFd);

            // 设置HTTP请求处理回调
            conn->setHttpCallback([this](HttpRequest& req, HttpResponse& res) {
                onHttpRequest(req, res);
            });

            // 设置连接关闭回调
            conn->setCloseCallback([this, connId](const std::shared_ptr<HttpConnection>&) {
                mainLoop_->runInLoop([this, connId] {
                    connections_.erase(connId);
                });
            });

            connections_[connId] = conn;

            // 在SubReactor的线程中启动连接
            ioLoop->runInLoop([conn] {
                conn->start();
            });

            // 继续接受下一个连接（边缘触发模式需要）
            connFd = SocketUtils::acceptConnection(listenFd_, &clientAddr);
        }
    }

    /**
     * HTTP请求处理 - 路由分发
     */
    void onHttpRequest(HttpRequest& req, HttpResponse& res) {
        // 通过路由器处理请求
        if (!router_->handle(req, res)) {
            // 没有匹配的路由，返回404
            res.status(404)
               .type("text/html; charset=utf-8")
               .send("<html><body>"
                     "<h1>404 Not Found</h1>"
                     "<p>Cannot " + req.methodString() + " " + req.path() + "</p>"
                     "<hr><p>CppExpress/1.0</p>"
                     "</body></html>");
        }
    }

    int numThreads_;
    std::atomic<bool> started_;
    std::atomic<int> nextConnId_;
    uint16_t port_;
    std::string host_;

    socket_t listenFd_;
    std::unique_ptr<EventLoop> mainLoop_;
    std::unique_ptr<EventLoopThreadPool> threadPool_;
    std::unique_ptr<Channel> listenChannel_;

    std::shared_ptr<Router> router_;
    std::unordered_map<int, std::shared_ptr<HttpConnection>> connections_;
};

// 类型别名，更像Express
using CppExpress = HttpServer;

} // namespace cppexpress
