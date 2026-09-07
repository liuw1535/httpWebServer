# CppExpress 🚀

一个基于 **C++17** 的 Express 风格 HTTP 服务器框架。

## ✨ 特性

- **一主多从 Reactor 模型** - MainReactor 负责接受连接，多个 SubReactor 负责 I/O 处理
- **epoll 边缘触发** - Linux 上使用 epoll ET 模式实现高性能 I/O 多路复用
- **有限状态机 HTTP 解析** - 高效的分段式 HTTP 请求解析器
- **正则表达式路由** - 支持路径参数（`:id`）和查询参数提取
- **小对象内存池** - 多级内存池设计，有效减少内存碎片
- **Express 风格 API** - 路由注册、中间件链、子路由器
- **长连接支持** - HTTP/1.1 Keep-Alive
- **静态文件服务** - 内置静态文件中间件
- **跨平台** - 支持 Linux（epoll）、Windows（select）、macOS（select）

## 📁 项目结构

```
cppexpress/
├── CMakeLists.txt              # CMake 构建配置
├── README.md                   # 项目说明
├── include/
│   ├── cppexpress.h            # 统一头文件
│   └── cppexpress/
│       ├── platform.h          # 平台抽象层
│       ├── memory_pool.h       # 小对象内存池
│       ├── logger.h            # 日志系统
│       ├── buffer.h            # I/O 缓冲区
│       ├── thread_pool.h       # 线程池
│       ├── channel.h           # 事件通道
│       ├── epoller.h           # I/O 多路复用器
│       ├── event_loop.h        # 事件循环
│       ├── event_loop_thread_pool.h  # 事件循环线程池
│       ├── http_parser.h       # HTTP 请求解析器（有限状态机）
│       ├── http_response.h     # HTTP 响应构建器
│       ├── router.h            # 路由系统 & 中间件
│       ├── http_connection.h   # HTTP 连接管理
│       ├── static_files.h      # 静态文件中间件
│       └── http_server.h       # 服务器主类
├── src/
│   └── cppexpress.cpp          # 库编译单元
└── examples/
    └── main.cpp                # 示例程序
```

## 🏗️ 架构设计

### Reactor 模型

```
                    ┌─────────────────┐
                    │   MainReactor   │
                    │  (accept连接)    │
                    └────────┬────────┘
                             │ 轮询分发
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
     ┌────────────┐ ┌────────────┐ ┌────────────┐
     │ SubReactor │ │ SubReactor │ │ SubReactor │
     │  (I/O处理)  │ │  (I/O处理)  │ │  (I/O处理)  │
     └────────────┘ └────────────┘ └────────────┘
```

### HTTP 解析状态机

```
REQUEST_LINE ──→ HEADERS ──→ BODY ──→ COMPLETE
                                  └──→ ERROR
```

### 内存池结构

```
MemoryPool
├── FixedSizePool (8 bytes)
├── FixedSizePool (16 bytes)
├── FixedSizePool (32 bytes)
├── FixedSizePool (64 bytes)
├── FixedSizePool (128 bytes)
├── FixedSizePool (256 bytes)
├── FixedSizePool (512 bytes)
└── FixedSizePool (1024 bytes)
```

## 🔧 构建

### 依赖

- CMake >= 3.14
- C++17 兼容编译器（GCC 7+, Clang 5+, MSVC 2017+）

### Linux / macOS

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Windows (MSVC)

```powershell
mkdir build; cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

### Windows (MinGW)

```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
mingw32-make -j4
```

## 🚀 快速开始

```cpp
#include "cppexpress.h"

using namespace cppexpress;

int main() {
    CppExpress app(4);  // 4个工作线程

    // GET 路由
    app.get("/", [](HttpRequest& req, HttpResponse& res) {
        res.send("Hello, CppExpress!");
    });

    // JSON 响应
    app.get("/api/data", [](HttpRequest& req, HttpResponse& res) {
        res.json(R"({"message": "Hello!"})");
    });

    // 路径参数
    app.get("/users/:id", [](HttpRequest& req, HttpResponse& res) {
        std::string id = req.param("id");
        res.json(R"({"userId": ")" + id + R"("})");
    });

    // 查询参数
    app.get("/search", [](HttpRequest& req, HttpResponse& res) {
        std::string q = req.query("q", "");
        res.json(R"({"query": ")" + q + R"("})");
    });

    // POST 请求
    app.post("/api/data", [](HttpRequest& req, HttpResponse& res) {
        res.json(R"({"received": ")" + req.body() + R"("})");
    });

    // 启动服务器
    app.listen(3000);
    return 0;
}
```

## 📖 API 参考

### 路由

```cpp
app.get(path, handler);      // GET
app.post(path, handler);     // POST
app.put(path, handler);      // PUT
app.del(path, handler);      // DELETE
app.patch(path, handler);    // PATCH
app.head(path, handler);     // HEAD
app.options(path, handler);  // OPTIONS
app.all(path, handler);      // 所有方法
```

### 中间件

```cpp
// 全局中间件
app.use([](HttpRequest& req, HttpResponse& res, NextFunction next) {
    // 前置处理
    next();  // 调用下一个中间件
    // 后置处理
});

// 路径中间件
app.use("/api", middleware);

// 内置中间件
app.use(middleware::logger());     // 请求日志
app.use(middleware::cors());       // CORS 支持
```

### 子路由器

```cpp
auto apiRouter = std::make_shared<Router>();
apiRouter->get("/users", handler);
apiRouter->post("/users", handler);
app.use("/api/v1", apiRouter);
```

### 静态文件

```cpp
app.serveStatic("./public");           // 根路径
app.serveStatic("./assets", "/static"); // 指定前缀
```

### 请求对象 (HttpRequest)

```cpp
req.method();              // HTTP 方法
req.path();                // 请求路径
req.url();                 // 完整 URL
req.param("id");           // 路径参数
req.query("key", "default"); // 查询参数
req.header("Content-Type"); // 请求头
req.body();                // 请求体
req.keepAlive();           // 是否长连接
```

### 响应对象 (HttpResponse)

```cpp
res.status(200);           // 设置状态码
res.set("Key", "Value");   // 设置响应头
res.type("text/html");     // 设置 Content-Type
res.send("text");          // 发送文本
res.json("{}");            // 发送 JSON
res.html("<h1>Hi</h1>");   // 发送 HTML
res.redirect("/new-url");  // 重定向
res.sendFile("file.html"); // 发送文件
```

## 📝 技术细节

### 有限状态机 HTTP 解析

解析器使用三个状态：`REQUEST_LINE` → `HEADERS` → `BODY`，支持分段解析以适配非阻塞 I/O。请求行使用正则表达式 `^(\w+)\s+(\S+)\s+(HTTP/\d\.\d)$` 匹配，URI 使用 `^([^?#]+)(?:\?([^#]*))?(?:#.*)?$` 分离路径和查询字符串。

### 内存池

采用多级固定大小块内存池设计（8B ~ 1024B），每级使用空闲链表管理。小于等于 1024 字节的分配走内存池，大对象直接使用 `malloc`。提供 `PoolAllocator` 适配器可用于 STL 容器，以及 `PoolObject` 基类可通过继承自动使用内存池。

### 长连接

HTTP/1.1 默认启用 Keep-Alive，解析器在完成一个请求后自动重置状态，继续解析同一连接上的下一个请求。通过 `Connection: close` 头可以关闭长连接。

## 📄 License

MIT License
