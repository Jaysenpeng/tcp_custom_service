#ifndef TCP_SERVICE_BASE_H
#define TCP_SERVICE_BASE_H

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <iostream>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <map>
#include <mutex>

#include "telemetry.h"
#include "tcp_context_propagation.h"
#include "models.h"

/**
 * @brief TCP服务基类，提供基本的TCP服务生命周期管理和遥测集成
 * 使用优化的TCP上下文传播替代HTTP方式
 */
class TcpServiceBase {
public:
    /**
     * @brief 构造函数
     * @param service_name 服务名称
     * @param service_version 服务版本
     * @param host 主机地址
     * @param port 端口
     */
    TcpServiceBase(const std::string& service_name, const std::string& service_version,
                   const std::string& host, int port)
        : service_name_(service_name), service_version_(service_version),
          host_(host), port_(port), running_(false), server_socket_(-1) {
    }
    
    /**
     * @brief 析构函数
     */
    virtual ~TcpServiceBase() {
        Stop();
    }
    
    /**
     * @brief 启动服务
     */
    virtual void Start() {
        // 初始化OpenTelemetry
        Telemetry::InitTelemetry(service_name_, service_version_);
        
        // 注册处理器
        RegisterHandlers();
        
        // 创建TCP监听socket
        server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket_ < 0) {
            throw std::runtime_error("创建socket失败");
        }
        
        // 设置地址重用
        int opt = 1;
        setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        // 绑定地址
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr(host_.c_str());
        server_addr.sin_port = htons(port_);
        
        if (bind(server_socket_, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(server_socket_);
            throw std::runtime_error("绑定地址失败");
        }
        
        // 开始监听
        if (listen(server_socket_, 10) < 0) {
            close(server_socket_);
            throw std::runtime_error("监听失败");
        }
        
        running_ = true;
        std::cout << "TCP服务 " << service_name_ << " 运行于 " << host_ << ":" << port_ << std::endl;
        
        // 创建服务器线程
        server_thread_ = std::thread([this]() {
            ServerLoop();
        });
        
        // 创建健康检查线程
        health_check_thread_ = std::thread([this]() {
            HealthCheckLoop();
        });
    }

    /**
     * @brief 停止服务
     */
    virtual void Stop() {
        if (!running_) {
            return;
        }

        running_ = false;
        
        if (server_socket_ >= 0) {
            close(server_socket_);
            server_socket_ = -1;
        }
        
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        
        if (health_check_thread_.joinable()) {
            health_check_thread_.join();
        }
        
        Telemetry::CleanupTelemetry();
        std::cout << "TCP服务 " << service_name_ << " 已停止" << std::endl;
    }

    /**
     * @brief 等待服务结束
     */
    void WaitForShutdown() {
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

protected:
    /**
     * @brief 注册消息处理器（子类重写）
     */
    virtual void RegisterHandlers() = 0;

    /**
     * @brief 注册处理器
     */
    template<typename RequestType, typename ResponseType>
    void RegisterHandler(const std::string& message_type,
                        std::function<ResponseType(const RequestType&)> handler) {
        handlers_[message_type] = [handler](const std::vector<uint8_t>& request_data) -> std::vector<uint8_t> {
            // 反序列化请求
            auto json_str = std::string(request_data.begin(), request_data.end());
            auto json_data = nlohmann::json::parse(json_str);
            RequestType request = json_data.get<RequestType>();
            
            // 处理请求
            auto response = handler(request);
            
            // 序列化响应
            nlohmann::json json_response = response;
            auto response_str = json_response.dump();
            return std::vector<uint8_t>(response_str.begin(), response_str.end());
        };
    }

    /**
     * @brief 发送TCP请求到其他服务
     */
    template<typename RequestType, typename ResponseType>
    ResponseType SendTcpRequest(const std::string& host, int port,
                               const std::string& message_type,
                               const RequestType& request) {
        // 创建连接
        int client_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (client_socket < 0) {
            throw std::runtime_error("创建客户端socket失败");
        }
        
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr(host.c_str());
        server_addr.sin_port = htons(port);
        
        if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(client_socket);
            throw std::runtime_error("连接服务失败");
        }
        
        try {
            // 获取当前追踪上下文
            auto trace_data = tcp_context_propagation::GetCurrentTraceContextBinary();
            
            // 构造消息格式: [trace_data_size(4)][trace_data][msg_type_size(4)][msg_type][data_size(4)][data]
            std::vector<uint8_t> message;
            
            // 追踪数据大小
            uint32_t trace_size = htonl(static_cast<uint32_t>(trace_data.size()));
            message.insert(message.end(), (uint8_t*)&trace_size, (uint8_t*)&trace_size + 4);
            
            // 追踪数据
            message.insert(message.end(), trace_data.begin(), trace_data.end());
            
            // 消息类型大小
            uint32_t msg_type_size = htonl(static_cast<uint32_t>(message_type.size()));
            message.insert(message.end(), (uint8_t*)&msg_type_size, (uint8_t*)&msg_type_size + 4);
            
            // 消息类型
            message.insert(message.end(), message_type.begin(), message_type.end());
            
            // 序列化请求数据
            nlohmann::json json_request = request;
            auto request_str = json_request.dump();
            std::vector<uint8_t> request_data(request_str.begin(), request_str.end());
            
            // 数据大小
            uint32_t data_size = htonl(static_cast<uint32_t>(request_data.size()));
            message.insert(message.end(), (uint8_t*)&data_size, (uint8_t*)&data_size + 4);
            
            // 数据
            message.insert(message.end(), request_data.begin(), request_data.end());
            
            // 发送消息
            send(client_socket, message.data(), message.size(), 0);
            
            // 接收响应大小
            uint32_t response_size;
            recv(client_socket, &response_size, 4, MSG_WAITALL);
            response_size = ntohl(response_size);
            
            // 接收响应数据
            std::vector<uint8_t> response_data(response_size);
            recv(client_socket, response_data.data(), response_size, MSG_WAITALL);
            
            // 反序列化响应
            auto json_str = std::string(response_data.begin(), response_data.end());
            auto json_data = nlohmann::json::parse(json_str);
            ResponseType response = json_data.get<ResponseType>();
            
            close(client_socket);
            return response;
            
        } catch (...) {
            close(client_socket);
            throw;
        }
    }

private:
    /**
     * @brief 服务器主循环
     */
    void ServerLoop() {
        while (running_) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            
            int client_socket = accept(server_socket_, (sockaddr*)&client_addr, &client_len);
            if (client_socket < 0) {
                if (running_) {
                    std::cerr << "接受连接失败" << std::endl;
                }
                continue;
            }
            
            // 处理客户端连接
            std::thread([this, client_socket]() {
                HandleClient(client_socket);
            }).detach();
        }
    }
    
    /**
     * @brief 处理客户端连接
     */
    void HandleClient(int client_socket) {
        try {
            // 读取追踪数据大小
            uint32_t trace_size;
            if (recv(client_socket, &trace_size, 4, MSG_WAITALL) != 4) {
                close(client_socket);
                return;
            }
            trace_size = ntohl(trace_size);
            
            // 读取追踪数据
            std::vector<uint8_t> trace_data(trace_size);
            if (recv(client_socket, trace_data.data(), trace_size, MSG_WAITALL) != trace_size) {
                close(client_socket);
                return;
            }
            
            // 应用追踪上下文
            auto context_token = tcp_context_propagation::SetTraceContextFromBinary(trace_data);
            
            // 读取消息类型大小
            uint32_t msg_type_size;
            if (recv(client_socket, &msg_type_size, 4, MSG_WAITALL) != 4) {
                close(client_socket);
                return;
            }
            msg_type_size = ntohl(msg_type_size);
            
            // 读取消息类型
            std::vector<uint8_t> msg_type_data(msg_type_size);
            if (recv(client_socket, msg_type_data.data(), msg_type_size, MSG_WAITALL) != msg_type_size) {
                close(client_socket);
                return;
            }
            std::string message_type(msg_type_data.begin(), msg_type_data.end());
            
            // 读取数据大小
            uint32_t data_size;
            if (recv(client_socket, &data_size, 4, MSG_WAITALL) != 4) {
                close(client_socket);
                return;
            }
            data_size = ntohl(data_size);
            
            // 读取数据
            std::vector<uint8_t> request_data(data_size);
            if (recv(client_socket, request_data.data(), data_size, MSG_WAITALL) != data_size) {
                close(client_socket);
                return;
            }
            
            // 创建span进行追踪
            auto scope = CreateSpan(service_name_ + "." + message_type);
            auto span = GetCurrentSpan();
            
            span->SetAttribute("message.type", message_type);
            span->SetAttribute("service.name", service_name_);
            span->SetAttribute("protocol", "tcp");
            
            // 处理请求
            std::vector<uint8_t> response_data;
            
            auto handler_it = handlers_.find(message_type);
            if (handler_it != handlers_.end()) {
                try {
                    response_data = handler_it->second(request_data);
                    span->SetStatus(trace::StatusCode::kOk);
                } catch (const std::exception& e) {
                    span->SetStatus(trace::StatusCode::kError, e.what());
                    
                    // 创建错误响应
                    nlohmann::json error_response = {
                        {"success", false},
                        {"message", e.what()}
                    };
                    auto error_str = error_response.dump();
                    response_data = std::vector<uint8_t>(error_str.begin(), error_str.end());
                }
            } else {
                span->SetStatus(trace::StatusCode::kError, "未知消息类型");
                
                nlohmann::json error_response = {
                    {"success", false},
                    {"message", "未知消息类型: " + message_type}
                };
                auto error_str = error_response.dump();
                response_data = std::vector<uint8_t>(error_str.begin(), error_str.end());
            }
            
            // 发送响应大小
            uint32_t response_size = htonl(static_cast<uint32_t>(response_data.size()));
            send(client_socket, &response_size, 4, 0);
            
            // 发送响应数据
            send(client_socket, response_data.data(), response_data.size(), 0);
            
        } catch (const std::exception& e) {
            std::cerr << "处理客户端连接时出错: " << e.what() << std::endl;
        }
        
        close(client_socket);
    }
    
    /**
     * @brief 健康检查循环
     */
    void HealthCheckLoop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            if (running_) {
                auto scope = CreateSpan("health_check");
                auto span = GetCurrentSpan();
                span->SetAttribute("service.name", service_name_);
                span->SetAttribute("health.status", "healthy");
            }
        }
    }

protected:
    std::string service_name_;
    std::string service_version_;
    std::string host_;
    int port_;
    std::atomic<bool> running_;
    
    int server_socket_;
    std::thread server_thread_;
    std::thread health_check_thread_;
    
    // 消息处理器映射
    std::map<std::string, std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>> handlers_;
    std::mutex handlers_mutex_;
};

#endif // TCP_SERVICE_BASE_H