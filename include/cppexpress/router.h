#pragma once

/**
 * Express风格的路由系�?
 * 支持路径参数（如 /user/:id�?
 * 支持中间件链
 * 支持路由分组
 */

#include <string>
#include <vector>
#include <functional>
#include <regex>
#include <memory>
#include <unordered_map>
#include "http_parser.h"
#include "http_response.h"
#include "logger.h"

namespace cppexpress {

// 前向声明
class Router;

/**
 * 中间�?路由处理函数类型
 * next() 调用下一个中间件
 */
using NextFunction = std::function<void()>;
using HandlerFunction = std::function<void(HttpRequest&, HttpResponse&)>;
using MiddlewareFunction = std::function<void(HttpRequest&, HttpResponse&, NextFunction)>;

/**
 * 路由条目 - 存储一个路由的匹配规则和处理函�?
 */
struct Route {
    HttpMethod method;
    std::string pattern;        // 原始路径模式，如 /user/:id
    std::regex regex;           // 编译后的正则表达�?
    std::vector<std::string> paramNames;  // 参数名列�?
    HandlerFunction handler;

    Route(HttpMethod m, const std::string& p, HandlerFunction h)
        : method(m), pattern(p), handler(std::move(h)) {
        compilePattern();
    }

private:
    /**
     * 将Express风格的路径模式编译为正则表达�?
     * /user/:id -> /user/([^/]+)
     * /files/[star]  -> /files/([star])
     */
    void compilePattern() {
        std::string regexStr = "^";
        
        // 使用正则表达式提取路径参�?
        static const std::regex paramRegex(R"(:(\w+))");
        static const std::regex wildcardRegex(R"(\*)");
        
        std::string remaining = pattern;
        std::smatch match;
        // size_t lastPos = 0; // reserved for future use
        std::string temp = pattern;

        // 先处理参�?
        while (std::regex_search(temp, match, paramRegex)) {
            // 添加参数前的固定部分（转义特殊字符）
            std::string prefix = match.prefix().str();
            regexStr += escapeRegex(prefix);
            
            // 添加参数捕获�?
            paramNames.push_back(match[1].str());
            regexStr += "([^/]+)";
            
            temp = match.suffix().str();
        }
        
        // 添加剩余部分
        regexStr += escapeRegex(temp);
        
        // 处理通配�?
        std::string finalRegex;
        for (size_t i = 0; i < regexStr.size(); ++i) {
            if (regexStr[i] == '*') {
                finalRegex += "(.*)";
            } else {
                finalRegex += regexStr[i];
            }
        }
        
        finalRegex += "$";
        regex = std::regex(finalRegex);
    }

    static std::string escapeRegex(const std::string& str) {
        static const std::regex specialChars(R"([-[\]{}()+?.,\\^$|#\s])");
        return std::regex_replace(str, specialChars, R"(\$&)");
    }
};

/**
 * 中间件条�?
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

    // 所有方�?
    Router& all(const std::string& path, HandlerFunction handler) {
        addRoute(HttpMethod::GET, path, handler);
        addRoute(HttpMethod::POST, path, handler);
        addRoute(HttpMethod::PUT, path, handler);
        addRoute(HttpMethod::DELETE_, path, handler);
        addRoute(HttpMethod::PATCH, path, handler);
        return *this;
    }

    // 添加中间�?
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
     * 处理请求 - 执行中间件链和路由匹�?
     */
    bool handle(HttpRequest& req, HttpResponse& res) {
        // 构建中间�?+ 路由处理�?
        std::vector<MiddlewareFunction> chain;

        // 收集匹配的中间件
        for (const auto& mw : middlewares_) {
            if (mw.path.empty() || pathStartsWith(req.path(), mw.path)) {
                chain.push_back(mw.handler);
            }
        }

        // 查找匹配的路�?
        HandlerFunction routeHandler = nullptr;
        for (auto& route : routes_) {
            if (matchRoute(route, req)) {
                routeHandler = route.handler;
                break;
            }
        }

        // 检查子路由�?
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
     * 匹配路由 - 使用正则表达式匹配路径并提取参数
     */
    bool matchRoute(Route& route, HttpRequest& req) {
        if (route.method != req.method()) return false;

        std::smatch match;
        std::string path = req.path();
        if (std::regex_match(path, match, route.regex)) {
            // 提取路径参数
            for (size_t i = 0; i < route.paramNames.size() && i + 1 < match.size(); ++i) {
                req.setParam(route.paramNames[i], match[i + 1].str());
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
 * 内置中间�?
 */
namespace middleware {

/**
 * CORS中间�?
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
 * 请求日志中间�?
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

/**
 * JSON解析中间件（简单实现）
 */
inline MiddlewareFunction jsonParser() {
    return [](HttpRequest& req, HttpResponse& res, NextFunction next) {
        (void)res;
        auto contentType = req.header("Content-Type");
        if (contentType.find("application/json") != std::string::npos) {
            // body已经在HttpParser中解析了，这里只做标�?
            // 实际的JSON解析可以使用第三方库如nlohmann/json
        }
        next();
    };
}

} // namespace middleware

} // namespace cppexpress
