/**
 * CppExpress 示例程序
 * 展示Express风格的HTTP服务器用法
 */

#include "cppexpress.h"
#include <iostream>
#include <sstream>

using namespace cppexpress;

int main() {
    // 创建服务器实例（4个工作线程）
    CppExpress app(4);

    // 设置日志级别
    app.setLogLevel(LogLevel::LVL_INFO);

    // ============ 中间件============

    // 使用日志中间件
    app.use(middleware::logger());

    // 使用CORS中间件
    app.use(middleware::cors());

    // 自定义中间件：添加响应头
    app.use([](HttpRequest& req, HttpResponse& res, NextFunction next) {
        (void)req;
        res.set("X-Powered-By", "CppExpress/1.0");
        next();
    });

    // ============ 路由 ============

    // 首页
    app.get("/", [](HttpRequest& req, HttpResponse& res) {
        (void)req;
        res.html(R"(
            <!DOCTYPE html>
            <html>
            <head>
                <title>CppExpress</title>
                <style>
                    body { font-family: Arial, sans-serif; max-width: 800px; margin: 50px auto; padding: 20px; }
                    h1 { color: #333; }
                    code { background: #f4f4f4; padding: 2px 6px; border-radius: 3px; }
                    .endpoint { margin: 10px 0; padding: 10px; background: #f9f9f9; border-left: 3px solid #007bff; }
                    .method { font-weight: bold; color: #007bff; }
                </style>
            </head>
            <body>
                <h1>🚀 CppExpress Server</h1>
                <p>A C++17 Express-style HTTP Server is running!</p>
                <h2>Available Endpoints:</h2>
                <div class="endpoint"><span class="method">GET</span> <code>/</code> - This page</div>
                <div class="endpoint"><span class="method">GET</span> <code>/api/hello</code> - Hello JSON</div>
                <div class="endpoint"><span class="method">GET</span> <code>/api/users/:id</code> - Get user by ID</div>
                <div class="endpoint"><span class="method">POST</span> <code>/api/echo</code> - Echo request body</div>
                <div class="endpoint"><span class="method">GET</span> <code>/api/search?q=keyword</code> - Search with query params</div>
            </body>
            </html>
        )");
    });

    // JSON API
    app.get("/api/hello", [](HttpRequest& req, HttpResponse& res) {
        (void)req;
        res.json(R"({"message": "Hello from CppExpress!", "version": "1.0.0"})");
    });

    // 路径参数示例
    app.get("/api/users/:id", [](HttpRequest& req, HttpResponse& res) {
        std::string id = req.param("id");
        std::ostringstream json;
        json << R"({"user": {"id": )" << id
             << R"(, "name": "User)" << id << R"(", "email": "user)" << id << R"(@example.com"}})";
        res.json(json.str());
    });

    // POST请求 - Echo
    app.post("/api/echo", [](HttpRequest& req, HttpResponse& res) {
        std::ostringstream json;
        json << R"({"echo": ")" << req.body()
             << R"(", "method": ")" << req.methodString()
             << R"(", "content-type": ")" << req.header("Content-Type")
             << R"("})";
        res.json(json.str());
    });

    // 查询参数示例
    app.get("/api/search", [](HttpRequest& req, HttpResponse& res) {
        std::string query = req.query("q", "");
        int page = 1;
        std::string pageStr = req.query("page", "1");
        try { page = std::stoi(pageStr); } catch (...) {}

        std::ostringstream json;
        json << R"({"query": ")" << query
             << R"(", "page": )" << page
             << R"(, "results": []})";
        res.json(json.str());
    });

    // 子路由器示例
    auto apiRouter = std::make_shared<Router>();
    apiRouter->get("/status", [](HttpRequest& req, HttpResponse& res) {
        (void)req;
        res.json(R"({"status": "ok", "uptime": "running"})");
    });
    apiRouter->get("/version", [](HttpRequest& req, HttpResponse& res) {
        (void)req;
        res.json(R"({"version": "1.0.0", "cpp_standard": "C++17"})");
    });
    app.use("/api/v1", apiRouter);

    // 重定向示例?
    app.get("/old-page", [](HttpRequest& req, HttpResponse& res) {
        (void)req;
        res.redirect("/");
    });

    // ============ 启动服务============
    
    std::cout << R"(
   ____            _____                              
  / ___|_ __  _ __| ____|_  ___ __  _ __ ___  ___ ___ 
 | |   | '_ \| '_ \  _| \ \/ / '_ \| '__/ _ \/ __/ __|
 | |___| |_) | |_) | |___ >  <| |_) | | |  __/\__ \__ \
  \____|| .__/| .__/|_____/_/\_\ .__/|_|  \___||___/___/
        |_|   |_|              |_|                       
    )" << std::endl;

    app.listen(3000, "0.0.0.0", []() {
        std::cout << "Server is ready to accept connections!" << std::endl;
    });

    return 0;
}
