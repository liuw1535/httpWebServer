/**
 * CppExpress 库编译单元
 * 由于大部分实现在头文件中（header-only风格），
 * 这个文件主要用于确保静态库能正确编译
 */

#include "cppexpress.h"

namespace cppexpress {

// 确保Buffer中的静态成员被定义

// 库版本信息
const char* version() {
    return "1.0.0";
}

const char* description() {
    return "CppExpress - A C++17 Express-style HTTP Server";
}

} // namespace cppexpress
