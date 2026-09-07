#pragma once

/**
 * 简单高效的日志系统
 * 支持多级别日志输出，线程安全
 */

#include <iostream>
#include <sstream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <ctime>

namespace cppexpress {

enum class LogLevel {
    LVL_TRACE = 0,
    LVL_DEBUG = 1,
    LVL_INFO  = 2,
    LVL_WARN  = 3,
    LVL_ERROR = 4,
    LVL_FATAL = 5,
    LVL_OFF   = 6
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void setLevel(LogLevel level) { level_ = level; }
    LogLevel getLevel() const { return level_; }

    void log(LogLevel level, const std::string& file, int line, const std::string& msg) {
        if (level < level_) return;

        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tm_buf;
#ifdef PLATFORM_WINDOWS
        localtime_s(&tm_buf, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_buf);
#endif

        // 提取文件名
        std::string filename = file;
        auto pos = filename.find_last_of("/\\");
        if (pos != std::string::npos) {
            filename = filename.substr(pos + 1);
        }

        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count()
            << " [" << levelToString(level) << "] "
            << filename << ":" << line << " - "
            << msg;

        std::lock_guard<std::mutex> lock(mutex_);
        if (level >= LogLevel::LVL_ERROR) {
            std::cerr << oss.str() << std::endl;
        } else {
            std::cout << oss.str() << std::endl;
        }
    }

private:
    Logger() : level_(LogLevel::LVL_INFO) {}

    static const char* levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::LVL_TRACE: return "TRACE";
            case LogLevel::LVL_DEBUG: return "DEBUG";
            case LogLevel::LVL_INFO:  return "INFO ";
            case LogLevel::LVL_WARN:  return "WARN ";
            case LogLevel::LVL_ERROR: return "ERROR";
            case LogLevel::LVL_FATAL: return "FATAL";
            default: return "?????";
        }
    }

    LogLevel level_;
    std::mutex mutex_;
};

} // namespace cppexpress

// 日志宏 - 在命名空间外定义，使用完全限定名
#define LOG_TRACE(msg) do { \
    std::ostringstream _log_oss; _log_oss << msg; \
    ::cppexpress::Logger::instance().log(::cppexpress::LogLevel::LVL_TRACE, __FILE__, __LINE__, _log_oss.str()); \
} while(0)

#define LOG_DEBUG(msg) do { \
    std::ostringstream _log_oss; _log_oss << msg; \
    ::cppexpress::Logger::instance().log(::cppexpress::LogLevel::LVL_DEBUG, __FILE__, __LINE__, _log_oss.str()); \
} while(0)

#define LOG_INFO(msg) do { \
    std::ostringstream _log_oss; _log_oss << msg; \
    ::cppexpress::Logger::instance().log(::cppexpress::LogLevel::LVL_INFO, __FILE__, __LINE__, _log_oss.str()); \
} while(0)

#define LOG_WARN(msg) do { \
    std::ostringstream _log_oss; _log_oss << msg; \
    ::cppexpress::Logger::instance().log(::cppexpress::LogLevel::LVL_WARN, __FILE__, __LINE__, _log_oss.str()); \
} while(0)

#define LOG_ERROR(msg) do { \
    std::ostringstream _log_oss; _log_oss << msg; \
    ::cppexpress::Logger::instance().log(::cppexpress::LogLevel::LVL_ERROR, __FILE__, __LINE__, _log_oss.str()); \
} while(0)

#define LOG_FATAL(msg) do { \
    std::ostringstream _log_oss; _log_oss << msg; \
    ::cppexpress::Logger::instance().log(::cppexpress::LogLevel::LVL_FATAL, __FILE__, __LINE__, _log_oss.str()); \
} while(0)
