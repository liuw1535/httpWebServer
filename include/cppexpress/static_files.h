#pragma once

/**
 * 静态文件服务中间件
 * 类似Express的express.static()
 */

#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include "router.h"
#include "http_response.h"
#include "logger.h"

namespace cppexpress {
namespace middleware {

namespace fs = std::filesystem;

/**
 * 静态文件服务中间件
 * @param root 静态文件根目录
 * @param prefix URL前缀（默认为空，匹配所有路径）
 */
inline MiddlewareFunction staticFiles(const std::string& root, const std::string& prefix = "") {
    return [root, prefix](HttpRequest& req, HttpResponse& res, NextFunction next) {
        // 只处理GET和HEAD请求
        if (req.method() != HttpMethod::GET && req.method() != HttpMethod::HEAD) {
            next();
            return;
        }

        std::string reqPath = req.path();
        
        // 检查前缀
        if (!prefix.empty()) {
            if (reqPath.find(prefix) != 0) {
                next();
                return;
            }
            reqPath = reqPath.substr(prefix.size());
        }

        // 安全检查：防止路径遍历攻击
        if (reqPath.find("..") != std::string::npos) {
            res.status(403).send("Forbidden");
            return;
        }

        // 构建文件路径
        std::string filePath = root;
        if (!filePath.empty() && filePath.back() != '/' && filePath.back() != '\\') {
            filePath += '/';
        }
        if (!reqPath.empty() && reqPath.front() == '/') {
            reqPath = reqPath.substr(1);
        }
        filePath += reqPath;

        // 如果是目录，尝试index.html
        try {
            if (fs::is_directory(filePath)) {
                filePath += "/index.html";
            }
        } catch (...) {
            // 忽略文件系统错误
        }

        // 检查文件是否存在
        try {
            if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
                next();
                return;
            }
        } catch (...) {
            next();
            return;
        }

        // 读取文件
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            next();
            return;
        }

        // 获取文件大小
        file.seekg(0, std::ios::end);
        auto fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        // 读取文件内容
        std::string content(static_cast<size_t>(fileSize), '\0');
        file.read(&content[0], fileSize);

        // 获取MIME类型
        std::string ext;
        auto dotPos = filePath.rfind('.');
        if (dotPos != std::string::npos) {
            ext = filePath.substr(dotPos);
        }

        // 设置缓存头
        res.set("Cache-Control", "public, max-age=3600");
        
        // 发送响应
        res.type(getMimeType(ext)).status(200).send(content);
    };
}

} // namespace middleware
} // namespace cppexpress
