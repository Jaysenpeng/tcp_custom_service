# TCP API网关
add_executable(tcp-api-gateway
    main.cc
    ../common/telemetry.h
    ../common/tcp_context_propagation.h
    ../common/context_propagation.h
    ../common/models.h
    tcp_gateway_service.h
)

# 链接库
target_link_libraries(tcp-api-gateway
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

message(STATUS "配置TCP API网关 - HTTP前端:8080 -> TCP后端")