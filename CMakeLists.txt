cmake_minimum_required(VERSION 3.10)
project(TCPTraceTest)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖包
find_package(opentelemetry-cpp CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

# 子目录
add_subdirectory(user-service)
add_subdirectory(message-service)
add_subdirectory(notification-service)
add_subdirectory(api-gateway)
add_subdirectory(chat-client)

# 配置Zipkin端点
set(ZIPKIN_HOST "192.168.159.138" CACHE STRING "Zipkin服务器地址")
set(ZIPKIN_PORT "9411" CACHE STRING "Zipkin服务器端口")
add_definitions(-DZIPKIN_HOST="${ZIPKIN_HOST}")
add_definitions(-DZIPKIN_PORT="${ZIPKIN_PORT}")
