#include "tcp_user_service.h"
#include <iostream>
#include <signal.h>

// 全局服务实例
std::unique_ptr<TcpUserService> g_service;

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
        std::cout << "=== TCP 用户服务 v1.0.0 ===" << std::endl;
        std::cout << "使用优化的TCP上下文传播进行分布式追踪" << std::endl;
        
        // 解析命令行参数
        std::string host = "127.0.0.1";
        int port = 8081;
        
        if (argc >= 2) {
            host = argv[1];
        }
        if (argc >= 3) {
            port = std::stoi(argv[2]);
        }
        
        std::cout << "启动参数:" << std::endl;
        std::cout << "- 主机: " << host << std::endl;
        std::cout << "- 端口: " << port << std::endl;
        
        // 创建服务实例
        g_service = std::make_unique<TcpUserService>(host, port);
        
        // 启动服务
        g_service->Start();
        
        std::cout << "TCP用户服务启动成功！" << std::endl;
        std::cout << "支持的消息类型:" << std::endl;
        std::cout << "- user.register: 用户注册" << std::endl;
        std::cout << "- user.login: 用户登录" << std::endl;
        std::cout << "- user.get: 获取用户信息" << std::endl;
        std::cout << "按 Ctrl+C 停止服务" << std::endl;
        
        // 等待服务结束
        g_service->WaitForShutdown();
        
    } catch (const std::exception& e) {
        std::cerr << "服务启动失败: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}