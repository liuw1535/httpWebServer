#pragma once

/**
 * HTTP响应构建器
 * 支持链式调用，类似Express的res对象
 */

#include <string>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include "buffer.h"
#include "http_parser.h"

namespace cppexpress {

// MIME类型映射
inline std::string getMimeType(const std::string& ext) {
    static const std::unordered_map<std::string, std::string> mimeTypes = {
        {".html", "text/html"},
        {".htm",  "text/html"},
        {".css",  "text/css"},
        {".js",   "application/javascript"},
        {".json", "application/json"},
        {".xml",  "application/xml"},
        {".txt",  "text/plain"},
        {".png",  "image/png"},
        {".jpg",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif",  "image/gif"},
        {".svg",  "image/svg+xml"},
        {".ico",  "image/x-icon"},
        {".webp", "image/webp"},
        {".mp4",  "video/mp4"},
        {".mp3",  "audio/mpeg"},
        {".pdf",  "application/pdf"},
        {".zip",  "application/zip"},
        {".woff", "font/woff"},
        {".woff2","font/woff2"},
        {".ttf",  "font/ttf"},
    };

    auto it = mimeTypes.find(ext);
    if (it != mimeTypes.end()) {
        return it->second;
    }
    return "application/octet-stream";
}

// HTTP状态码描述
inline std::string getStatusMessage(int code) {
    static const std::unordered_map<int, std::string> statusMessages = {
        {100, "Continue"},
        {200, "OK"},
        {201, "Created"},
        {204, "No Content"},
        {301, "Moved Permanently"},
        {302, "Found"},
        {304, "Not Modified"},
        {400, "Bad Request"},
        {401, "Unauthorized"},
        {403, "Forbidden"},
        {404, "Not Found"},
        {405, "Method Not Allowed"},
        {408, "Request Timeout"},
        {413, "Payload Too Large"},
        {500, "Internal Server Error"},
        {502, "Bad Gateway"},
        {503, "Service Unavailable"},
    };

    auto it = statusMessages.find(code);
    if (it != statusMessages.end()) {
        return it->second;
    }
    return "Unknown";
}

/**
 * HTTP响应对象 - Express风格的API
 */
class HttpResponse {
public:
    HttpResponse(HttpVersion version = HttpVersion::HTTP_11, bool keepAlive = true)
        : version_(version)
        , statusCode_(200)
        , keepAlive_(keepAlive)
        , sent_(false) {
    }

    // 设置状态码（链式调用）
    HttpResponse& status(int code) {
        statusCode_ = code;
        return *this;
    }

    // 设置响应头（链式调用）
    HttpResponse& set(const std::string& key, const std::string& value) {
        headers_[key] = value;
        return *this;
    }

    // 设置Content-Type（链式调用）
    HttpResponse& type(const std::string& contentType) {
        headers_["Content-Type"] = contentType;
        return *this;
    }

    // 发送文本响应
    HttpResponse& send(const std::string& body) {
        body_ = body;
        if (headers_.find("Content-Type") == headers_.end()) {
            headers_["Content-Type"] = "text/plain; charset=utf-8";
        }
        sent_ = true;
        return *this;
    }

    // 发送JSON响应
    HttpResponse& json(const std::string& jsonStr) {
        body_ = jsonStr;
        headers_["Content-Type"] = "application/json; charset=utf-8";
        sent_ = true;
        return *this;
    }

    // 发送HTML响应
    HttpResponse& html(const std::string& htmlStr) {
        body_ = htmlStr;
        headers_["Content-Type"] = "text/html; charset=utf-8";
        sent_ = true;
        return *this;
    }

    // 重定向
    HttpResponse& redirect(const std::string& url, int code = 302) {
        statusCode_ = code;
        headers_["Location"] = url;
        body_.clear();
        sent_ = true;
        return *this;
    }

    // 发送文件
    HttpResponse& sendFile(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            statusCode_ = 404;
            body_ = "File not found";
            headers_["Content-Type"] = "text/plain";
            sent_ = true;
            return *this;
        }

        // 读取文件内容
        std::ostringstream oss;
        oss << file.rdbuf();
        body_ = oss.str();

        // 根据扩展名设置MIME类型
        auto dotPos = filepath.rfind('.');
        if (dotPos != std::string::npos) {
            std::string ext = filepath.substr(dotPos);
            headers_["Content-Type"] = getMimeType(ext);
        }

        sent_ = true;
        return *this;
    }

    // 构建响应字符串
    std::string build() const {
        std::ostringstream oss;

        // 状态行
        oss << httpVersionToString(version_) << " "
            << statusCode_ << " "
            << getStatusMessage(statusCode_) << "\r\n";

        // 响应头
        auto headers = headers_;
        headers["Content-Length"] = std::to_string(body_.size());
        
        if (keepAlive_) {
            headers["Connection"] = "keep-alive";
            headers["Keep-Alive"] = "timeout=60, max=1000";
        } else {
            headers["Connection"] = "close";
        }

        if (headers.find("Server") == headers.end()) {
            headers["Server"] = "CppExpress/1.0";
        }

        for (const auto& [key, value] : headers) {
            oss << key << ": " << value << "\r\n";
        }

        oss << "\r\n";
        oss << body_;

        return oss.str();
    }

    // 写入Buffer
    void writeTo(Buffer& buffer) const {
        std::string response = build();
        buffer.append(response);
    }

    int statusCode() const { return statusCode_; }
    bool keepAlive() const { return keepAlive_; }
    void setKeepAlive(bool on) { keepAlive_ = on; }
    bool isSent() const { return sent_; }
    const std::string& body() const { return body_; }

private:
    HttpVersion version_;
    int statusCode_;
    bool keepAlive_;
    bool sent_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};

} // namespace cppexpress
