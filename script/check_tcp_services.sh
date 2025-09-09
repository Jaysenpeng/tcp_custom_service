#!/bin/bash

# TCP聊天系统服务状态检查脚本
# 检查所有TCP服务的运行状态

# 设置工作目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"

cd "$BUILD_DIR" || {
    echo "错误: 无法进入构建目录 $BUILD_DIR"
    exit 1
}

# ANSI颜色代码
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 检查服务状态的函数
check_service() {
    local service_name=$1
    local service_port=$2
    local pid_file="${service_name}.pid"
    local status="STOPPED"
    local pid=""
    local port_status="❌"
    
    # 检查PID文件
    if [ -f "$pid_file" ]; then
        pid=$(cat "$pid_file")
        if ps -p "$pid" > /dev/null 2>&1; then
            status="RUNNING"
        else
            status="DEAD"  # PID文件存在但进程不存在
        fi
    fi
    
    # 检查端口占用
    if [ -n "$service_port" ]; then
        if netstat -tuln 2>/dev/null | grep -q ":$service_port "; then
            port_status="✅"
        elif ss -tuln 2>/dev/null | grep -q ":$service_port "; then
            port_status="✅"
        fi
    fi
    
    # 输出状态
    printf "%-25s" "$service_name"
    
    case $status in
        "RUNNING")
            printf "${GREEN}%-12s${NC}" "RUNNING"
            printf "%-12s" "PID: $pid"
            ;;
        "DEAD")
            printf "${RED}%-12s${NC}" "DEAD"
            printf "%-12s" "PID: $pid"
            ;;
        "STOPPED")
            printf "${YELLOW}%-12s${NC}" "STOPPED"
            printf "%-12s" "-"
            ;;
    esac
    
    if [ -n "$service_port" ]; then
        printf "%-12s" "Port: $service_port"
        printf "%-8s" "$port_status"
    else
        printf "%-20s" "-"
    fi
    
    # 检查内存使用
    if [ "$status" = "RUNNING" ] && [ -n "$pid" ]; then
        local mem_usage=$(ps -o rss= -p "$pid" 2>/dev/null | tr -d ' ')
        if [ -n "$mem_usage" ]; then
            local mem_mb=$((mem_usage / 1024))
            printf "${BLUE}%s MB${NC}" "$mem_mb"
        fi
    fi
    
    echo
}

# 检查端口工具是否可用
check_port_tools() {
    if ! command -v netstat >/dev/null 2>&1 && ! command -v ss >/dev/null 2>&1; then
        echo "警告: netstat 和 ss 命令都不可用，无法检查端口状态"
        return 1
    fi
    return 0
}

# 主检查函数
main() {
    echo "=== TCP聊天系统服务状态 ==="
    echo "架构: HTTP前端 + TCP后端 + 31字节上下文传播"
    echo
    
    # 检查端口工具
    check_port_tools
    
    # 表头
    printf "%-25s %-12s %-12s %-12s %-8s %s\n" "服务名称" "状态" "进程ID" "端口" "端口状态" "内存"
    echo "--------------------------------------------------------------------------------"
    
    # 检查各个服务
    check_service "tcp-user-service" "8081"
    check_service "tcp-message-service" "8082"
    check_service "tcp-notification-service" "8083"
    check_service "tcp-api-gateway" "8080"
    
    echo
    
    # 统计信息
    local running_count=0
    local total_count=4
    
    for service in tcp-user-service tcp-message-service tcp-notification-service tcp-api-gateway; do
        local pid_file="${service}.pid"
        if [ -f "$pid_file" ]; then
            local pid=$(cat "$pid_file")
            if ps -p "$pid" > /dev/null 2>&1; then
                running_count=$((running_count + 1))
            fi
        fi
    done
    
    echo "=== 系统状态概览 ==="
    printf "运行中服务: ${GREEN}%d${NC}/%d\n" "$running_count" "$total_count"
    
    if [ "$running_count" -eq "$total_count" ]; then
        echo "状态: ${GREEN}✅ 所有服务正常运行${NC}"
        echo "特性: 31字节高效TCP上下文传播已激活"
    elif [ "$running_count" -eq 0 ]; then
        echo "状态: ${RED}❌ 所有服务已停止${NC}"
    else
        echo "状态: ${YELLOW}⚠️  部分服务运行中${NC}"
    fi
    
    # 显示系统资源使用
    echo
    echo "=== 系统资源 ==="
    
    # 显示内存使用总计
    local total_mem=0
    for service in tcp-user-service tcp-message-service tcp-notification-service tcp-api-gateway; do
        local pid_file="${service}.pid"
        if [ -f "$pid_file" ]; then
            local pid=$(cat "$pid_file")
            if ps -p "$pid" > /dev/null 2>&1; then
                local mem_usage=$(ps -o rss= -p "$pid" 2>/dev/null | tr -d ' ')
                if [ -n "$mem_usage" ]; then
                    total_mem=$((total_mem + mem_usage))
                fi
            fi
        fi
    done
    
    if [ "$total_mem" -gt 0 ]; then
        local total_mem_mb=$((total_mem / 1024))
        echo "总内存使用: ${BLUE}${total_mem_mb} MB${NC}"
    fi
    
    # 显示监听端口
    echo
    echo "=== 网络端口 ==="
    if command -v netstat >/dev/null 2>&1; then
        echo "TCP监听端口:"
        netstat -tuln 2>/dev/null | grep -E ":(8080|8081|8082|8083) " | while read line; do
            echo "  $line"
        done
    elif command -v ss >/dev/null 2>&1; then
        echo "TCP监听端口:"
        ss -tuln 2>/dev/null | grep -E ":(8080|8081|8082|8083) " | while read line; do
            echo "  $line"
        done
    fi
    
    echo
    echo "🔍 检查完成 - $(date)"
}

# 执行主函数
main