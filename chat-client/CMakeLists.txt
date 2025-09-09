# TCP聊天客户端
add_executable(tcp-chat-client
    tcp_chat_client.cc
    ../common/telemetry.h
    ../common/context_propagation.h
    ../common/models.h
)

# 链接库
target_link_libraries(tcp-chat-client
    PRIVATE
    opentelemetry_exporter_zipkin_trace
    opentelemetry_trace
    opentelemetry_common
    opentelemetry_resources
    opentelemetry_version
    opentelemetry_http_client_curl
    nlohmann_json::nlohmann_json
    curl
    Threads::Threads
)

message(STATUS "配置TCP聊天客户端 - 连接到API网关")