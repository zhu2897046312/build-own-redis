cmake_minimum_required(VERSION 3.13)

project(redis-starter-cpp)

# 添加头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

file(GLOB_RECURSE SOURCE_FILES src/*.cpp)

set(CMAKE_CXX_STANDARD 23) # Enable the C++23 standard
set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Threads REQUIRED)
#find_package(asio CONFIG REQUIRED)

add_executable(server ${SOURCE_FILES})

# 设置编译选项
target_compile_options(server PRIVATE -Wall -Wextra)

# target_link_libraries(server PRIVATE asio asio::asio)
target_link_libraries(server PRIVATE Threads::Threads)

# 设置输出目录
set_target_properties(server
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)