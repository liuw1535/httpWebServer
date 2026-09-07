#pragma once

/**
 * HTTP请求解析器 - 基于有限状态机
 * 支持分段解析（适配非阻塞I/O）
 * 使用正则表达式提取URI和参数
 */

#include <string>
#include <unordered_map>
#include <regex>
#include <algorithm>
#include <sstream>
#include "buffer.h"
#include "logger.h"

namespace cppexpress {

// HTTP方法
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE_,
    PATCH,
    HEAD,
    OPTIONS,
    UNKNOWN
};

inline std::string httpMethodToString(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET:     return "GET";
        case HttpMethod::POST:    return "POST";
        case HttpMethod::PUT:     return "PUT";
        case HttpMethod::DELETE_: return "DELETE";
        case HttpMethod::PATCH:   return "PATCH";
        case HttpMethod::HEAD:    return "HEAD";
        case HttpMethod::OPTIONS: return "OPTIONS";
        default: return "UNKNOWN";
    }
}

inline HttpMethod stringToHttpMethod(const std::string& method) {
    if (method == "GET")     return HttpMethod::GET;
    if (method == "POST")    return HttpMethod::POST;
    if (method == "PUT")     return HttpMethod::PUT;
    if (method == "DELETE")  return HttpMethod::DELETE_;
    if (method == "PATCH")   return HttpMethod::PATCH;
    if (method == "HEAD")    return HttpMethod::HEAD;
    if (method == "OPTIONS") return HttpMethod::OPTIONS;
    return HttpMethod::UNKNOWN;
}

// HTTP版本
enum class HttpVersion {
    HTTP_10,
    HTTP_11,
    HTTP_20,
    UNKNOWN
};

inline std::string httpVersionToString(HttpVersion version) {
    switch (version) {
        case HttpVersion::HTTP_10: return "HTTP/1.0";
        case HttpVersion::HTTP_11: return "HTTP/1.1";
        case HttpVersion::HTTP_20: return "HTTP/2.0";
        default: return "HTTP/1.1";
    }
}

/**
 * HTTP请求对象
 */
class HttpRequest {
public:
    HttpRequest() : method_(HttpMethod::UNKNOWN), version_(HttpVersion::HTTP_11) {}

    // 方法
    void setMethod(HttpMethod method) { method_ = method; }
    HttpMethod method() const { return method_; }
    std::string methodString() const { return httpMethodToString(method_); }

    // URL和路径
    void setUrl(const std::string& url) { url_ = url; }
    const std::string& url() const { return url_; }

    void setPath(const std::string& path) { path_ = path; }
    const std::string& path() const { return path_; }

    // 版本
    void setVersion(HttpVersion version) { version_ = version; }
    HttpVersion version() const { return version_; }

    // 查询参数
    void setQuery(const std::string& key, const std::string& value) {
        query_[key] = value;
    }
    std::string query(const std::string& key, const std::string& defaultVal = "") const {
        auto it = query_.find(key);
        return it != query_.end() ? it->second : defaultVal;
    }
    const std::unordered_map<std::string, std::string>& queries() const { return query_; }

    // 路由参数（如 /user/:id 中的 id）
    void setParam(const std::string& key, const std::string& value) {
        params_[key] = value;
    }
    std::string param(const std::string& key, const std::string& defaultVal = "") const {
        auto it = params_.find(key);
        return it != params_.end() ? it->second : defaultVal;
    }
    const std::unordered_map<std::string, std::string>& params() const { return params_; }

    // 请求头
    void setHeader(const std::string& key, const std::string& value) {
        headers_[key] = value;
    }
    std::string header(const std::string& key, const std::string& defaultVal = "") const {
        auto it = headers_.find(key);
        return it != headers_.end() ? it->second : defaultVal;
    }
    const std::unordered_map<std::string, std::string>& headers() const { return headers_; }

    // 请求体
    void setBody(const std::string& body) { body_ = body; }
    void appendBody(const char* data, size_t len) { body_.append(data, len); }
    const std::string& body() const { return body_; }

    // Content-Length
    size_t contentLength() const {
        auto it = headers_.find("Content-Length");
        if (it != headers_.end()) {
            return std::stoull(it->second);
        }
        return 0;
    }

    // 是否保持连接
    bool keepAlive() const {
        auto it = headers_.find("Connection");
        if (it != headers_.end()) {
            if (version_ == HttpVersion::HTTP_11) {
                return it->second != "close";
            } else {
                return it->second == "keep-alive" || it->second == "Keep-Alive";
            }
        }
        return version_ == HttpVersion::HTTP_11;
    }

    // 重置
    void reset() {
        method_ = HttpMethod::UNKNOWN;
        version_ = HttpVersion::HTTP_11;
        url_.clear();
        path_.clear();
        query_.clear();
        params_.clear();
        headers_.clear();
        body_.clear();
    }

private:
    HttpMethod method_;
    HttpVersion version_;
    std::string url_;
    std::string path_;
    std::unordered_map<std::string, std::string> query_;
    std::unordered_map<std::string, std::string> params_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};

/**
 * HTTP请求解析器 - 有限状态机实现
 * 
 * 状态转换:
 * REQUEST_LINE -> HEADERS -> BODY -> COMPLETE
 *                                 -> ERROR
 */
class HttpParser {
public:
    // 解析状态
    enum class ParseState {
        REQUEST_LINE,   // 解析请求行
        HEADERS,        // 解析请求头
        BODY,           // 解析请求体
        COMPLETE,       // 解析完成
        ERROR           // 解析错误
    };

    // 解析结果
    enum class ParseResult {
        NEED_MORE,      // 需要更多数据
        COMPLETE,       // 解析完成
        ERROR           // 解析错误
    };

    HttpParser() : state_(ParseState::REQUEST_LINE) {}

    // 解析缓冲区中的数据
    ParseResult parse(Buffer& buffer, HttpRequest& request) {
        while (true) {
            switch (state_) {
                case ParseState::REQUEST_LINE: {
                    const char* crlf = buffer.findCRLF();
                    if (!crlf) {
                        return ParseResult::NEED_MORE;
                    }
                    std::string line(buffer.peek(), crlf);
                    buffer.retrieveUntil(crlf + 2);

                    if (!parseRequestLine(line, request)) {
                        state_ = ParseState::ERROR;
                        return ParseResult::ERROR;
                    }
                    state_ = ParseState::HEADERS;
                    break;
                }

                case ParseState::HEADERS: {
                    const char* crlf = buffer.findCRLF();
                    if (!crlf) {
                        return ParseResult::NEED_MORE;
                    }
                    std::string line(buffer.peek(), crlf);
                    buffer.retrieveUntil(crlf + 2);

                    if (line.empty()) {
                        // 空行，头部结束
                        size_t contentLen = request.contentLength();
                        if (contentLen > 0) {
                            state_ = ParseState::BODY;
                        } else {
                            state_ = ParseState::COMPLETE;
                            return ParseResult::COMPLETE;
                        }
                    } else {
                        if (!parseHeader(line, request)) {
                            state_ = ParseState::ERROR;
                            return ParseResult::ERROR;
                        }
                    }
                    break;
                }

                case ParseState::BODY: {
                    size_t contentLen = request.contentLength();
                    size_t bodyNeeded = contentLen - request.body().size();
                    
                    if (buffer.readableBytes() < bodyNeeded) {
                        // 数据不够，先读取已有的
                        request.appendBody(buffer.peek(), buffer.readableBytes());
                        buffer.retrieveAll();
                        return ParseResult::NEED_MORE;
                    }

                    request.appendBody(buffer.peek(), bodyNeeded);
                    buffer.retrieve(bodyNeeded);
                    state_ = ParseState::COMPLETE;
                    return ParseResult::COMPLETE;
                }

                case ParseState::COMPLETE:
                    return ParseResult::COMPLETE;

                case ParseState::ERROR:
                    return ParseResult::ERROR;
            }
        }
    }

    // 重置解析器状态
    void reset() {
        state_ = ParseState::REQUEST_LINE;
    }

    ParseState state() const { return state_; }

private:
    /**
     * 解析请求行: METHOD URI HTTP/VERSION
     * 使用正则表达式提取
     */
    bool parseRequestLine(const std::string& line, HttpRequest& request) {
        // 正则表达式匹配请求行
        static const std::regex requestLineRegex(
            R"(^(\w+)\s+(\S+)\s+(HTTP/\d\.\d)$)"
        );

        std::smatch match;
        if (!std::regex_match(line, match, requestLineRegex)) {
            LOG_ERROR("Invalid request line: " << line);
            return false;
        }

        // 方法
        request.setMethod(stringToHttpMethod(match[1].str()));
        if (request.method() == HttpMethod::UNKNOWN) {
            LOG_ERROR("Unknown HTTP method: " << match[1].str());
            return false;
        }

        // URI（包含路径和查询参数）
        std::string uri = match[2].str();
        request.setUrl(uri);
        parseUri(uri, request);

        // 版本
        std::string version = match[3].str();
        if (version == "HTTP/1.0") {
            request.setVersion(HttpVersion::HTTP_10);
        } else if (version == "HTTP/1.1") {
            request.setVersion(HttpVersion::HTTP_11);
        } else if (version == "HTTP/2.0") {
            request.setVersion(HttpVersion::HTTP_20);
        }

        return true;
    }

    /**
     * 使用正则表达式解析URI，提取路径和查询参数
     * 例如: /api/users?name=john&age=30
     */
    void parseUri(const std::string& uri, HttpRequest& request) {
        // 正则表达式分离路径和查询字符串
        static const std::regex uriRegex(R"(^([^?#]+)(?:\?([^#]*))?(?:#.*)?$)");
        
        std::smatch match;
        if (std::regex_match(uri, match, uriRegex)) {
            // URL解码路径
            request.setPath(urlDecode(match[1].str()));

            // 解析查询参数
            if (match[2].matched) {
                parseQueryString(match[2].str(), request);
            }
        } else {
            request.setPath(uri);
        }
    }

    /**
     * 解析查询字符串: key1=value1&key2=value2
     */
    void parseQueryString(const std::string& queryString, HttpRequest& request) {
        // 正则表达式匹配每个键值对
        static const std::regex paramRegex(R"(([^&=]+)=([^&]*))");
        
        auto begin = std::sregex_iterator(queryString.begin(), queryString.end(), paramRegex);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            std::smatch match = *it;
            request.setQuery(urlDecode(match[1].str()), urlDecode(match[2].str()));
        }
    }

    /**
     * 解析请求头: Key: Value
     */
    bool parseHeader(const std::string& line, HttpRequest& request) {
        static const std::regex headerRegex(R"(^([^:]+):\s*(.*)$)");
        
        std::smatch match;
        if (std::regex_match(line, match, headerRegex)) {
            request.setHeader(match[1].str(), match[2].str());
            return true;
        }
        LOG_WARN("Invalid header line: " << line);
        return false;
    }

    /**
     * URL解码
     */
    static std::string urlDecode(const std::string& str) {
        std::string result;
        result.reserve(str.size());
        
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '%' && i + 2 < str.size()) {
                int hex = 0;
                std::istringstream iss(str.substr(i + 1, 2));
                if (iss >> std::hex >> hex) {
                    result += static_cast<char>(hex);
                    i += 2;
                } else {
                    result += str[i];
                }
            } else if (str[i] == '+') {
                result += ' ';
            } else {
                result += str[i];
            }
        }
        return result;
    }

    ParseState state_;
};

} // namespace cppexpress
