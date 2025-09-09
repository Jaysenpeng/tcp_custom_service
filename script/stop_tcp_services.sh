#!/bin/bash

# TCP聊天系统停服脚本
# 优雅停止所有TCP服务

# 设置工作目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"

cd "$BUILD_DIR" || {
    echo "错误: 无法进入构建目录 $BUILD_DIR"
    exit 1
}

# 停止服务的函数
stop_service() {
    local service_name=$1
    local pid_file="${service_name}.pid"
    
    if [ -f "$pid_file" ]; then
        local pid=$(cat "$pid_file")
        if ps -p "$pid" > /dev/null 2>&1; then
            echo "停止 $service_name (PID: $pid)..."
            kill "$pid"
            
            # 持续检查进程是否还在运行，直到完全停止
            local timeout=30  # 最大等待30秒
            local count=0
            echo -n "等待 $service_name 停止"
            
            while ps -p "$pid" > /dev/null 2>&1; do
                echo -n "."
                sleep 1
                count=$((count + 1))
                
                # 如果等待时间过长，使用强制杀死
                if [ $count -ge $timeout ]; then
                    echo ""
                    echo "警告: $service_name 未在预期时间内停止，使用强制杀死..."
                    kill -9 "$pid" 2>/dev/null || true
                    sleep 2
                    break
                fi
            done
            
            # 最终检查
            if ps -p "$pid" > /dev/null 2>&1; then
                echo ""
                echo "错误: $service_name (PID: $pid) 仍在运行！"
                return 1
            else
                echo ""
                echo "✓ $service_name 已完全停止"
                # 移除PID文件
                rm -f "$pid_file"
                return 0
            fi
        else
            echo "⚠ $service_name 未在运行"
            # 清理过期的PID文件
            rm -f "$pid_file"
            return 0
        fi
    else
        echo "⚠ $service_name 的PID文件不存在"
        return 0
    fi
}

# 按依赖顺序停止服务（先停依赖服务的，再停基础服务）
echo "=== 停止TCP聊天系统服务 ==="
echo "架构: HTTP前端 + TCP后端"
echo

# 1. 首先停止客户端连接的API网关
echo "1. 停止前端网关..."
stop_service tcp-api-gateway

sleep 1

# 2. 停止业务服务
echo
echo "2. 停止业务服务..."
stop_service tcp-notification-service
stop_service tcp-message-service

sleep 1

# 3. 最后停止基础的用户服务
echo
echo "3. 停止基础服务..."
stop_service tcp-user-service

echo
echo "=== 服务停止完成 ==="

# 额外的清理：查找并停止任何残留的相关进程
echo
echo "检查是否有残留进程..."
PIDS=$(ps aux | grep -E "(tcp-user-service|tcp-message-service|tcp-notification-service|tcp-api-gateway|tcp-chat-client)" | grep -v grep | awk '{print $2}')

if [ -n "$PIDS" ]; then
    echo "发现残留进程，正在清理..."
    for pid in $PIDS; do
        echo "强制停止进程 $pid"
        kill -9 "$pid" 2>/dev/null || true
    done
    sleep 1
    echo "残留进程清理完成"
else
    echo "✓ 没有发现残留进程"
fi

echo
echo "🎯 所有TCP服务已完全停止"
echo "特性: 31字节高效上下文传播已释放"