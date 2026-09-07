#pragma once

/**
 * CppExpress - C++17 Express风格HTTP服务器
 * 
 * 统一头文件，包含所有组件
 * 
 * 特性:
 * - 一主多从Reactor模型 + epoll边缘触发 + 线程池
 * - 有限状态机HTTP解析 + 正则表达式URI/参数提取
 * - 小对象内存池
 * - Express风格的路由和中间件
 * - 长连接支持
 * - 静态文件服务
 * - 跨平台（Linux/Windows/macOS）
 */

#include "cppexpress/platform.h"
#include "cppexpress/memory_pool.h"
#include "cppexpress/logger.h"
#include "cppexpress/buffer.h"
#include "cppexpress/thread_pool.h"
#include "cppexpress/channel.h"
#include "cppexpress/epoller.h"
#include "cppexpress/event_loop.h"
#include "cppexpress/event_loop_thread_pool.h"
#include "cppexpress/http_parser.h"
#include "cppexpress/http_response.h"
#include "cppexpress/router.h"
#include "cppexpress/http_connection.h"
#include "cppexpress/static_files.h"
#include "cppexpress/http_server.h"
