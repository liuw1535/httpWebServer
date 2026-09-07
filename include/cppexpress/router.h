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
     *   /files/*    -> LITERAL "/files/" + WILDCARD
     */
    void compilePattern() {
        // 在 pattern 上按 '/' 分段遍历
        size_t i = 0;
        while (i < pattern.size()) {
            if (pattern[i] == '*') {
                hasWildcard_ = true;
                break; // 通配符必须在末尾，剩余路径由 wildcard 消费
            }

            // 沿用 '/' 作为段分隔，但 literal 段允许跨多个 '/'（连续斜杠也照常匹配）
            // 这里改用逐段：每个段是一个 '/' 起头后到下一个 '/' 前的内容
            // 为简单起见，按 '/' 分段
            size_t segStart = i;
            // 找到下一个 '/' 的位置
            size_t segEnd = pattern.find('/', i + 1);
            if (segEnd == std::string::npos) segEnd = pattern.size();
            std::string seg = pattern.substr(segStart, segEnd - segStart);

            if (!seg.empty() && seg[0] == ':') {
                // 参数段
                Segment s;
                s.type = SegType::PARAM;
                s.paramName = seg.substr(1);
                segments_.push_back(std::move(s));
            } else if (!seg.empty() && seg[0] == '*') {
                hasWildcard_ = true;
                break;
            } else {
                // 字面量段（含前导 '/'）
                Segment s;
                s.type = SegType::LITERAL;
                s.literal = seg;
                segments_.push_back(std::move(s));
            }
            i = segEnd;
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
                // 字面量段必须精确匹配
                const std::string& lit = seg.literal;
                if (path.compare(pi, lit.size(), lit) != 0) {
                    return false;
                }
                pi += lit.size();
            } else if (seg.type == SegType::PARAM) {
                // 参数段消费到下一个 '/'
                // 前导 '/' 已包含在本段（compilePattern 中，PARAM 段不含 '/'，
                // 但其前的 '/' 属于前一段 LITERAL；首个段的 ':' 路径不合法，忽略）
                size_t slash = path.find('/', pi);
                if (slash == std::string::npos) {
                    // 后续还有段需要匹配，但 path 已无 '/'，仅当这是最后一段才合法
                    if (si + 1 != segments_.size()) return false;
                    params[seg.paramName] = path.substr(pi);
                    pi = path.size();
                    break;
                } else if (slash == pi) {
                    // 参数段不允许空值
                    return false;
                }
                params[seg.paramName] = path.substr(pi, slash - pi);
                pi = slash;
            }
        }

        // 通配段：消费剩余所有字符
        if (hasWildcard_) {
            // 通配符前应有 '/'，这里宽松匹配，剩余全部接受
            return true;
        }

        // 无通配：path 必须被完整消费
        return pi == path.size();
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
