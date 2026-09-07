#pragma once

/**
 * Express风格的路由
 * 支持路径参数（如 /user/:id
 * 支持中间件链
 * 支持路由分组
 */

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>
#include "http_parser.h"
#include "http_response.h"
#include "logger.h"

namespace cppexpress {

// 前向声明
class Router;

/**
 * 中间件路由处理函数类型
 * next() 调用下一个中间件
 */
using NextFunction = std::function<void()>;
using HandlerFunction = std::function<void(HttpRequest&, HttpResponse&)>;
using MiddlewareFunction = std::function<void(HttpRequest&, HttpResponse&, NextFunction)>;

/**
 * 路由条目 - 存储一个路由的匹配规则和处理函数
 *
 * 路由模式在注册时被预编译为一系列分段，匹配时按段扫描，
 * 避免 std::regex 在每请求热路径上的开销。
 */
struct Route {
    HttpMethod method;
    std::string pattern;        // 原始路径模式，如 /user/:id
    HandlerFunction handler;

    Route(HttpMethod m, const std::string& p, HandlerFunction h)
        : method(m), pattern(p), handler(std::move(h)) {
        compilePattern();
    }

private:
    friend class Router;

    // 一条路由被拆分为若干段，按出现顺序排列
    enum class SegType { LITERAL, PARAM, WILDCARD };
    struct Segment {
        SegType type;
        std::string literal;    // LITERAL 段的固定文本
        std::string paramName;   // PARAM 段的参数名
    };

    std::vector<Segment> segments_;
    bool hasWildcard_ = false;  // 末尾是否有通配段

    /**
     * 将 Express 风格的路径模式编译为分段
     *   /user/:id   -> LITERAL "/user/" + PARAM "id"
     *   /files/[*]  -> LITERAL "/files/" + WILDCARD
     *
     * 按 '/' 切分：每个 '/' 连同其后的段名一起构成一段。
     * ':' 起头的段名 => PARAM，'*' => WILDCARD，其余 => LITERAL。
     */
    void compilePattern() {
        size_t i = 0;
        while (i < pattern.size()) {
            char c = pattern[i];
            if (c == '*') {
                hasWildcard_ = true;
                break; // 通配符必须在末尾，剩余路径由 wildcard 消费
            }

            // 段以 '/' 开头；非 '/' 开头（如裸 "user"）按字面量处理
            if (c == '/') {
                // 收集 '/' 后的段名
                size_t nameStart = i + 1;
                size_t nameEnd = pattern.find('/', nameStart);
                if (nameEnd == std::string::npos) nameEnd = pattern.size();
                std::string name = pattern.substr(nameStart, nameEnd - nameStart);

                if (!name.empty() && name[0] == ':') {
                    Segment s;
                    s.type = SegType::PARAM;
                    s.paramName = name.substr(1);
                    s.literal = "/";   // 前导 '/'
                    segments_.push_back(std::move(s));
                } else if (!name.empty() && name[0] == '*') {
                    Segment s;
                    s.type = SegType::LITERAL;
                    s.literal = "/";
                    segments_.push_back(std::move(s));
                    hasWildcard_ = true;
                    break;
                } else {
                    Segment s;
                    s.type = SegType::LITERAL;
                    s.literal = "/" + name;
                    segments_.push_back(std::move(s));
                }
                i = nameEnd;
            } else {
                // 不以 '/' 开头的剩余部分，整体作为字面量
                size_t nameEnd = pattern.find('/', i);
                if (nameEnd == std::string::npos) nameEnd = pattern.size();
                Segment s;
                s.type = SegType::LITERAL;
                s.literal = pattern.substr(i, nameEnd - i);
                segments_.push_back(std::move(s));
                i = nameEnd;
            }
        }
    }

    /**
     * 匹配路径并提取参数
     * @param path 请求路径
     * @param params 输出参数键值对
     */
    bool matchAndExtract(const std::string& path,
                         std::unordered_map<std::string, std::string>& params) const {
        size_t pi = 0; // path 游标
        for (size_t si = 0; si < segments_.size(); ++si) {
            const Segment& seg = segments_[si];

            if (seg.type == SegType::LITERAL) {
                const std::string& lit = seg.literal;
                if (path.compare(pi, lit.size(), lit) != 0) return false;
                pi += lit.size();
            } else if (seg.type == SegType::PARAM) {
                // 段 literal 为前导 '/'，先匹配它
                if (path.compare(pi, seg.literal.size(), seg.literal) != 0) return false;
                pi += seg.literal.size();
                // 消费参数值到下一个 '/' 或结尾
                size_t slash = path.find('/', pi);
                if (slash == std::string::npos) {
                    // 到结尾，参数值为 path[pi..end)
                    if (pi == path.size()) return false; // 空参数值不合法
                    params[seg.paramName] = path.substr(pi);
                    pi = path.size();
                    // 后续还有段则无法匹配
                    if (si + 1 != segments_.size()) return false;
                    break;
                }
                if (slash == pi) return false; // 空参数值不合法
                params[seg.paramName] = path.substr(pi, slash - pi);
                pi = slash;
            }
        }

        if (hasWildcard_) return true; // 通配消费剩余
        return pi == path.size();     // 必须完整消费
    }
};

/**
 * 中间件条
 */
struct MiddlewareEntry {
    std::string path;           // 匹配路径前缀（空表示匹配所有）
    MiddlewareFunction handler;
};

/**
 * Router - Express风格的路由器
 */
class Router {
public:
    Router() = default;

    // HTTP方法路由注册
    Router& get(const std::string& path, HandlerFunction handler) {
        return addRoute(HttpMethod::GET, path, std::move(handler));
    }

    Router& post(const std::string& path, HandlerFunction handler) {
        return addRoute(HttpMethod::POST, path, std::move(handler));
    }

    Router& put(const std::string& path, HandlerFunction handler) {
        return addRoute(HttpMethod::PUT, path, std::move(handler));
    }

    Router& del(const std::string& path, HandlerFunction handler) {
        return addRoute(HttpMethod::DELETE_, path, std::move(handler));
    }

    Router& patch(const std::string& path, HandlerFunction handler) {
        return addRoute(HttpMethod::PATCH, path, std::move(handler));
    }

    Router& head(const std::string& path, HandlerFunction handler) {
        return addRoute(HttpMethod::HEAD, path, std::move(handler));
    }

    Router& options(const std::string& path, HandlerFunction handler) {
        return addRoute(HttpMethod::OPTIONS, path, std::move(handler));
    }

    // 所有方法
    Router& all(const std::string& path, HandlerFunction handler) {
        addRoute(HttpMethod::GET, path, handler);
        addRoute(HttpMethod::POST, path, handler);
        addRoute(HttpMethod::PUT, path, handler);
        addRoute(HttpMethod::DELETE_, path, handler);
        addRoute(HttpMethod::PATCH, path, handler);
        return *this;
    }

    // 添加中间件
    Router& use(MiddlewareFunction middleware) {
        middlewares_.push_back({"", std::move(middleware)});
        return *this;
    }

    Router& use(const std::string& path, MiddlewareFunction middleware) {
        middlewares_.push_back({path, std::move(middleware)});
        return *this;
    }

    // 挂载子路由器
    Router& use(const std::string& prefix, std::shared_ptr<Router> subRouter) {
        subRouters_.push_back({prefix, subRouter});
        return *this;
    }

    /**
     * 处理请求 - 执行中间件链和路由匹配
     */
    bool handle(HttpRequest& req, HttpResponse& res) {
        std::vector<MiddlewareFunction> chain;

        // 收集匹配的中间件
        for (const auto& mw : middlewares_) {
            if (mw.path.empty() || pathStartsWith(req.path(), mw.path)) {
                chain.push_back(mw.handler);
            }
        }

        // 查找匹配的路由
        HandlerFunction routeHandler = nullptr;
        for (auto& route : routes_) {
            if (matchRoute(route, req)) {
                routeHandler = route.handler;
                break;
            }
        }

        // 检查子路由
        if (!routeHandler) {
            for (auto& [prefix, subRouter] : subRouters_) {
                if (pathStartsWith(req.path(), prefix)) {
                    // 修改路径，去掉前缀
                    std::string originalPath = req.path();
                    std::string subPath = req.path().substr(prefix.size());
                    if (subPath.empty()) subPath = "/";
                    req.setPath(subPath);
                    
                    bool handled = subRouter->handle(req, res);
                    req.setPath(originalPath); // 恢复原始路径
                    if (handled) return true;
                }
            }
        }

        if (chain.empty() && !routeHandler) {
            return false; // 没有匹配的处理器
        }

        // 执行中间件链
        executeChain(chain, 0, req, res, routeHandler);
        return true;
    }

private:
    Router& addRoute(HttpMethod method, const std::string& path, HandlerFunction handler) {
        routes_.emplace_back(method, path, std::move(handler));
        return *this;
    }

    /**
     * 匹配路由 - 使用预编译分段匹配路径并提取参数
     */
    bool matchRoute(Route& route, HttpRequest& req) {
        if (route.method != req.method()) return false;

        std::unordered_map<std::string, std::string> params;
        if (route.matchAndExtract(req.path(), params)) {
            for (auto& [k, v] : params) {
                req.setParam(k, std::move(v));
            }
            return true;
        }
        return false;
    }

    /**
     * 递归执行中间件链
     */
    void executeChain(const std::vector<MiddlewareFunction>& chain,
                      size_t index,
                      HttpRequest& req,
                      HttpResponse& res,
                      HandlerFunction finalHandler) {
        if (res.isSent()) return;

        if (index < chain.size()) {
            chain[index](req, res, [&chain, index, &req, &res, &finalHandler, this]() {
                executeChain(chain, index + 1, req, res, finalHandler);
            });
        } else if (finalHandler) {
            finalHandler(req, res);
        }
    }

    bool pathStartsWith(const std::string& path, const std::string& prefix) {
        if (prefix.empty()) return true;
        if (path.size() < prefix.size()) return false;
        return path.compare(0, prefix.size(), prefix) == 0;
    }

    std::vector<Route> routes_;
    std::vector<MiddlewareEntry> middlewares_;
    std::vector<std::pair<std::string, std::shared_ptr<Router>>> subRouters_;
};

/**
 * 内置中间件
 */
namespace middleware {

/**
 * CORS中间件
 */
inline MiddlewareFunction cors(const std::string& origin = "*") {
    return [origin](HttpRequest& req, HttpResponse& res, NextFunction next) {
        res.set("Access-Control-Allow-Origin", origin);
        res.set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
        res.set("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.set("Access-Control-Max-Age", "86400");

        if (req.method() == HttpMethod::OPTIONS) {
            res.status(204).send("");
            return;
        }
        next();
    };
}

/**
 * 请求日志中间件
 */
inline MiddlewareFunction logger() {
    return [](HttpRequest& req, HttpResponse& res, NextFunction next) {
        auto start = std::chrono::steady_clock::now();
        LOG_INFO(req.methodString() << " " << req.path());
        
        next();

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        LOG_INFO(req.methodString() << " " << req.path()
                 << " " << res.statusCode()
                 << " " << duration.count() << "us");
    };
}

} // namespace middleware

} // namespace cppexpress
