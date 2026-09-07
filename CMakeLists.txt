cmake_minimum_required(VERSION 3.14)
project(cppexpress VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 平台检测
if(WIN32)
    add_definitions(-DPLATFORM_WINDOWS)
    add_definitions(-D_WIN32_WINNT=0x0601)
    add_definitions(-DNOMINMAX)
elseif(UNIX AND NOT APPLE)
    add_definitions(-DPLATFORM_LINUX)
elseif(APPLE)
    add_definitions(-DPLATFORM_MACOS)
endif()

# 编译选项
if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O2)
    endif()
endif()

# 源文件
file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "include/*.h" "include/*.hpp")

# 库目标
add_library(cppexpress STATIC ${SOURCES} ${HEADERS})
target_include_directories(cppexpress PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 平台链接库
if(WIN32)
    target_link_libraries(cppexpress PUBLIC ws2_32 mswsock)
elseif(UNIX)
    target_link_libraries(cppexpress PUBLIC pthread)
endif()

# 示例程序
add_executable(example examples/main.cpp)
target_link_libraries(example PRIVATE cppexpress)

# 安装
install(TARGETS cppexpress ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
