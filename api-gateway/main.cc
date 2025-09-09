#include "tcp_gateway_service.h"
#include <iostream>
#include <signal.h>

// 全局服务实例
std::unique_ptr<TcpGatewayService> g_service;

// 信号处理器
void signalHandler(int signum) {
    std::cout << "\n收到信号 " << signum << "，正在停止服务..." << std::endl;
    if (g_service) {
        g_service->Stop();
    }
    exit(signum);
}

int main(int argc, char* argv[]) {
    // 设置信号处理器
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    try {
        std::cout << "=== TCP API网关 v1.0.0 ===" << std::endl;
        std::cout << "HTTP前端 + TCP后端 + 优化的上下文传播" << std::endl;
        
        // 解析命令行参数
        std::string host = "127.0.0.1";
        int port = 8080;
        std::string user_service_host = "127.0.0.1";
        int user_service_port = 8081;
        std::string message_service_host = "127.0.0.1";
        int message_service_port = 8082;
        std::string notification_service_host = "127.0.0.1";
        int notification_service_port = 8083;
        
        if (argc >= 2) {
            host = argv[1];
        }
        if (argc >= 3) {
            port = std::stoi(argv[2]);
        }
        if (argc >= 4) {
            user_service_host = argv[3];
        }
        if (argc >= 5) {
            user_service_port = std::stoi(argv[4]);
        }
        if (argc >= 6) {
            message_service_host = argv[5];
        }
        if (argc >= 7) {
            message_service_port = std::stoi(argv[6]);
        }
        if (argc >= 8) {
            notification_service_host = argv[7];
        }
        if (argc >= 9) {
            notification_service_port = std::stoi(argv[8]);
        }
        
        std::cout << "启动参数:" << std::endl;
        std::cout << "- 网关: " << host << ":" << port << " (HTTP)" << std::endl;
        std::cout << "- 用户服务: " << user_service_host << ":" << user_service_port << " (TCP)" << std::endl;
        std::cout << "- 消息服务: " << message_service_host << ":" << message_service_port << " (TCP)" << std::endl;
        std::cout << "- 通知服务: " << notification_service_host << ":" << notification_service_port << " (TCP)" << std::endl;
        
        // 创建服务实例
        g_service = std::make_unique<TcpGatewayService>(
            "tcp-api-gateway", "1.0.0", host, port,
            user_service_host, user_service_port,
            message_service_host, message_service_port,
            notification_service_host, notification_service_port
        );
        
        // 启动服务
        g_service->Start();
        
        std::cout << "TCP API网关启动成功！" << std::endl;
        std::cout << "支持的API路由:" << std::endl;
        std::cout << "用户服务:" << std::endl;
        std::cout << "- POST /api/users/register: 用户注册" << std::endl;
        std::cout << "- POST /api/users/login: 用户登录" << std::endl;
        std::cout << "- GET  /api/users/{id}: 获取用户信息" << std::endl;
        std::cout << "消息服务:" << std::endl;
        std::cout << "- POST /api/messages/send: 发送消息" << std::endl;
        std::cout << "- GET  /api/messages: 获取消息列表" << std::endl;
        std::cout << "- POST /api/messages/mark_read: 标记消息已读" << std::endl;
        std::cout << "通知服务:" << std::endl;
        std::cout << "- POST /api/notifications/send: 发送通知" << std::endl;
        std::cout << "- GET  /api/notifications: 获取通知列表" << std::endl;
        std::cout << "特性: HTTP到TCP上下文自动转换，31字节高效传输" << std::endl;
        std::cout << "按 Ctrl+C 停止服务" << std::endl;
        
        // 等待服务结束
        g_service->WaitForShutdown();
        
    } catch (const std::exception& e) {
        std::cerr << "服务启动失败: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}