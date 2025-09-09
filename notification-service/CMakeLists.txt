# TCP通知服务
add_executable(tcp-notification-service
    main.cc
    ../common/telemetry.h
    ../common/tcp_context_propagation.h
    ../common/tcp_service_base.h
    ../common/models.h
    tcp_notification_service.h
)

# 链接库
target_link_libraries(tcp-notification-service
    PRIVATE
    opentelemetry_exporter_zipkin_trace
    opentelemetry_trace
    opentelemetry_common
    opentelemetry_resources
    opentelemetry_version
    opentelemetry_http_client_curl
    nlohmann_json::nlohmann_json
    uuid
    curl
    Threads::Threads
) 

message(STATUS "配置TCP通知服务 - 端口:8083")