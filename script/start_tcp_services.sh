#!/bin/bash

# TCP聊天系统启动脚本
# 使用优化的31字节TCP上下文传播

echo "=== 启动TCP聊天系统 ==="
echo "架构: HTTP前端 + TCP后端 + 优化的上下文传播"
echo

# 检查构建目录
if [ ! -d "../build" ]; then
    echo "错误: 构建目录不存在. 请先运行 'mkdir build && cd build && cmake .. && make'"
    exit 1
fi

cd ../build

# 检查是否有服务已在运行
check_running_services() {
    local running_services=()
    
    for service in tcp-user-service tcp-message-service tcp-notification-service tcp-api-gateway; do
        if [ -f "${service}.pid" ]; then
            local pid=$(cat "${service}.pid")
            if ps -p "$pid" > /dev/null 2>&1; then
                running_services+=("$service (PID: $pid)")
            else
                # 清理过期的PID文件
                rm -f "${service}.pid"
            fi
        fi
    done
    
    if [ ${#running_services[@]} -gt 0 ]; then
        echo "⚠️  检测到以下服务正在运行:"
        for service in "${running_services[@]}"; do
            echo "  - $service"
        done
        echo
        read -p "是否停止现有服务并重新启动? (y/N): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            echo "停止现有服务..."
            ../script/stop_tcp_services.sh
            sleep 2
        else
            echo "取消启动"
            exit 1
        fi
    fi
}

echo "检查现有服务状态..."
check_running_services

# 启动后端TCP服务
echo "启动TCP后端服务..."

echo "启动用户服务 (TCP:8081)..."
./user-service/tcp-user-service &
USER_PID=$!
echo $USER_PID > tcp-user-service.pid
echo "  PID: $USER_PID"

sleep 1

echo "启动消息服务 (TCP:8082)..."
./message-service/tcp-message-service &
MESSAGE_PID=$!
echo $MESSAGE_PID > tcp-message-service.pid
echo "  PID: $MESSAGE_PID"

sleep 1

echo "启动通知服务 (TCP:8083)..."
./notification-service/tcp-notification-service &
NOTIFICATION_PID=$!
echo $NOTIFICATION_PID > tcp-notification-service.pid
echo "  PID: $NOTIFICATION_PID"

sleep 2

# 启动API网关
echo "启动API网关 (HTTP:8080 -> TCP后端)..."
./api-gateway/tcp-api-gateway &
GATEWAY_PID=$!
echo $GATEWAY_PID > tcp-api-gateway.pid
echo "  PID: $GATEWAY_PID"

sleep 2

echo
echo "=== 所有服务已启动 ==="
echo "服务拓扑:"
echo "  客户端 (HTTP) -> API网关:8080 (HTTP->TCP转换) -> 后端服务 (TCP)"
echo "  ├── 用户服务: 127.0.0.1:8081 (TCP)"
echo "  ├── 消息服务: 127.0.0.1:8082 (TCP)"
echo "  └── 通知服务: 127.0.0.1:8083 (TCP)"
echo
echo "特性:"
echo "  ✓ 31字节高效TCP上下文传播"
echo "  ✓ HTTP到TCP追踪上下文无缝转换"
echo "  ✓ 分布式追踪 (Zipkin: http://localhost:9411)"
echo "  ✓ 去除HTTP依赖的纯TCP后端"
echo
echo "启动聊天客户端:"
echo "  ./chat-client/tcp-chat-client"
echo
echo "管理命令:"
echo "  检查服务状态: ../script/check_tcp_services.sh"
echo "  停止所有服务: ../script/stop_tcp_services.sh"
echo "  重启服务: 先停止再启动"
echo
echo "按Ctrl+C停止所有服务"

# 信号处理
cleanup() {
    echo
    echo "正在停止所有服务..."
    
    # 停止服务进程
    kill $USER_PID $MESSAGE_PID $NOTIFICATION_PID $GATEWAY_PID 2>/dev/null
    
    # 等待进程结束
    wait
    
    # 清理PID文件
    rm -f tcp-user-service.pid tcp-message-service.pid tcp-notification-service.pid tcp-api-gateway.pid
    
    echo "所有服务已停止，PID文件已清理"
    exit 0
}

trap cleanup INT TERM

# 等待
wait