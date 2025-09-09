#ifndef TCP_GATEWAY_SERVICE_H
#define TCP_GATEWAY_SERVICE_H

#include <string>
#include <memory>
#include <iostream>
#include <functional>
#include <map>
#include <regex>

#include "../third_party/httplib.h"
#include "../common/telemetry.h"
#include "../common/context_propagation.h"
#include "../common/tcp_context_propagation.h"
#include "../common/models.h"

/**
 * @brief TCP网关服务类
 * 接收HTTP请求，转换为TCP调用，同时处理HTTP到TCP的上下文传播
 */
class TcpGatewayService {
public:
    /**
     * @brief 构造函数
     */
    TcpGatewayService(const std::string& service_name, const std::string& service_version,
                      const std::string& host, int port,
                      const std::string& user_service_host, int user_service_port,
                      const std::string& message_service_host, int message_service_port,
                      const std::string& notification_service_host, int notification_service_port)
        : service_name_(service_name), service_version_(service_version),
          host_(host), port_(port), running_(false),
          user_service_host_(user_service_host), user_service_port_(user_service_port),
          message_service_host_(message_service_host), message_service_port_(message_service_port),
          notification_service_host_(notification_service_host), notification_service_port_(notification_service_port) {
        
        server_ = std::make_unique<httplib::Server>();
        SetupMiddleware();
    }

    /**
     * @brief 析构函数
     */
    virtual ~TcpGatewayService() {
        Stop();
    }

    /**
     * @brief 启动服务
     */
    virtual void Start() {
        // 初始化OpenTelemetry
        Telemetry::InitTelemetry(service_name_, service_version_);
        
        // 注册路由
        RegisterRoutes();
        
        running_ = true;
        std::cout << "TCP网关服务 " << service_name_ << " 运行于 " << host_ << ":" << port_ << std::endl;
        std::cout << "后端TCP服务:" << std::endl;
        std::cout << "- 用户服务: " << user_service_host_ << ":" << user_service_port_ << std::endl;
        std::cout << "- 消息服务: " << message_service_host_ << ":" << message_service_port_ << std::endl;
        std::cout << "- 通知服务: " << notification_service_host_ << ":" << notification_service_port_ << std::endl;
        
        // 启动HTTP服务器
        server_thread_ = std::thread([this]() {
            server_->listen(host_, port_);
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
        
        if (server_) {
            server_->stop();
        }
        
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        
        Telemetry::CleanupTelemetry();
        std::cout << "TCP网关服务 " << service_name_ << " 已停止" << std::endl;
    }

    /**
     * @brief 等待服务结束
     */
    void WaitForShutdown() {
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

private:
    /**
     * @brief 设置中间件
     */
    void SetupMiddleware() {
        // CORS中间件
        server_->set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, traceparent, tracestate");
            
            if (req.method == "OPTIONS") {
                res.status = 200;
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
    }

    /**
     * @brief 注册路由
     */
    void RegisterRoutes() {
        // 健康检查
        server_->Get("/health", [this](const httplib::Request&, httplib::Response& res) {
            nlohmann::json health = {
                {"status", "healthy"},
                {"service", "tcp-api-gateway"},
                {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()}
            };
            res.set_content(health.dump(), "application/json");
        });

        // 用户服务路由
        server_->Post("/api/users/register", 
            CreateTcpHandler<chat::models::RegisterRequest, chat::models::RegisterResponse>(
                "gateway.user_register", user_service_host_, user_service_port_, "user.register"));
        
        server_->Post("/api/users/login",
            CreateTcpHandler<chat::models::LoginRequest, chat::models::LoginResponse>(
                "gateway.user_login", user_service_host_, user_service_port_, "user.login"));
        
        server_->Get("/api/users/(.*)", 
            CreateTcpGetHandler<chat::models::UserInfo, chat::models::GetUserRequest>(
                "gateway.user_get", user_service_host_, user_service_port_, "user.get",
                [](const httplib::Request& req) -> chat::models::GetUserRequest {
                    std::smatch match;
                    std::regex pattern("/api/users/(.+)");
                    if (std::regex_search(req.path, match, pattern)) {
                        chat::models::GetUserRequest request;
                        request.user_id = match[1].str();
                        return request;
                    }
                    throw std::runtime_error("无效的用户ID");
                }));

        // 消息服务路由
        server_->Post("/api/messages/send",
            CreateTcpHandler<chat::models::SendMessageRequest, chat::models::SendMessageResponse>(
                "gateway.message_send", message_service_host_, message_service_port_, "message.send"));
        
        server_->Get("/api/messages",
            CreateTcpGetHandler<chat::models::GetMessagesResponse, chat::models::GetMessagesRequest>(
                "gateway.message_get", message_service_host_, message_service_port_, "message.get",
                [](const httplib::Request& req) -> chat::models::GetMessagesRequest {
                    chat::models::GetMessagesRequest request;
                    request.user_id = req.get_param_value("user_id");
                    request.other_user_id = req.get_param_value("other_user_id");
                    if (req.has_param("limit")) {
                        request.limit = std::stoi(req.get_param_value("limit"));
                    }
                    return request;
                }));
        
        server_->Post("/api/messages/mark_read",
            CreateTcpHandler<chat::models::MarkMessageReadRequest, chat::models::MarkMessageReadResponse>(
                "gateway.message_mark_read", message_service_host_, message_service_port_, "message.mark_read"));

        // 通知服务路由
        server_->Post("/api/notifications/send",
            CreateTcpHandler<chat::models::NotificationRequest, chat::models::NotificationResponse>(
                "gateway.notification_send", notification_service_host_, notification_service_port_, "notification.send"));
        
        server_->Get("/api/notifications",
            CreateTcpGetHandler<chat::models::GetNotificationsResponse, chat::models::GetNotificationsRequest>(
                "gateway.notification_get", notification_service_host_, notification_service_port_, "notification.get",
                [](const httplib::Request& req) -> chat::models::GetNotificationsRequest {
                    chat::models::GetNotificationsRequest request;
                    request.user_id = req.get_param_value("user_id");
                    if (req.has_param("limit")) {
                        request.limit = std::stoi(req.get_param_value("limit"));
                    }
                    return request;
                }));
    }

    /**
     * @brief 创建TCP处理器（用于POST请求）
     */
    template<typename RequestType, typename ResponseType>
    std::function<void(const httplib::Request&, httplib::Response&)> 
    CreateTcpHandler(const std::string& operation_name,
                     const std::string& tcp_host, int tcp_port,
                     const std::string& message_type) {
        return [this, operation_name, tcp_host, tcp_port, message_type]
               (const httplib::Request& req, httplib::Response& res) {
            
            // 提取HTTP追踪上下文并转换为TCP上下文
            auto http_context_token = context_propagation::ExtractHttpContext(req);
            
            // 创建span
            auto scope = CreateSpan(operation_name);
            auto span = GetCurrentSpan();
            
            span->SetAttribute("http.method", req.method);
            span->SetAttribute("http.url", req.path);
            span->SetAttribute("service.name", service_name_);
            span->SetAttribute("backend.service", tcp_host + ":" + std::to_string(tcp_port));
            span->SetAttribute("backend.message_type", message_type);
            span->SetAttribute("protocol.frontend", "http");
            span->SetAttribute("protocol.backend", "tcp");
            
            try {
                // 解析HTTP请求
                RequestType request;
                if (!req.body.empty()) {
                    auto json_data = nlohmann::json::parse(req.body);
                    request = json_data.get<RequestType>();
                }
                
                // 通过TCP调用后端服务
                span->AddEvent("calling_backend_service");
                auto response = SendTcpRequest<RequestType, ResponseType>(tcp_host, tcp_port, message_type, request);
                
                // 序列化响应
                nlohmann::json json_response = response;
                res.set_content(json_response.dump(), "application/json");
                res.status = 200;
                
                span->SetStatus(trace::StatusCode::kOk);
                span->AddEvent("backend_call_completed");
                
            } catch (const std::exception& e) {
                // 记录异常
                span->SetStatus(trace::StatusCode::kError, e.what());
                span->AddEvent("backend_call_failed", {
                    {"exception.type", typeid(e).name()},
                    {"exception.message", e.what()}
                });
                
                nlohmann::json error_response = {
                    {"success", false},
                    {"message", e.what()}
                };
                res.set_content(error_response.dump(), "application/json");
                res.status = 500;
            }
        };
    }

    /**
     * @brief 创建TCP GET处理器
     */
    template<typename ResponseType, typename RequestType>
    std::function<void(const httplib::Request&, httplib::Response&)> 
    CreateTcpGetHandler(const std::string& operation_name,
                        const std::string& tcp_host, int tcp_port,
                        const std::string& message_type,
                        std::function<RequestType(const httplib::Request&)> request_builder) {
        return [this, operation_name, tcp_host, tcp_port, message_type, request_builder]
               (const httplib::Request& req, httplib::Response& res) {
            
            // 提取HTTP追踪上下文
            auto http_context_token = context_propagation::ExtractHttpContext(req);
            
            // 创建span
            auto scope = CreateSpan(operation_name);
            auto span = GetCurrentSpan();
            
            span->SetAttribute("http.method", req.method);
            span->SetAttribute("http.url", req.path);
            span->SetAttribute("service.name", service_name_);
            span->SetAttribute("backend.service", tcp_host + ":" + std::to_string(tcp_port));
            span->SetAttribute("backend.message_type", message_type);
            span->SetAttribute("protocol.frontend", "http");
            span->SetAttribute("protocol.backend", "tcp");
            
            try {
                // 构建请求
                auto request = request_builder(req);
                
                // 通过TCP调用后端服务
                span->AddEvent("calling_backend_service");
                auto response = SendTcpRequest<RequestType, ResponseType>(tcp_host, tcp_port, message_type, request);
                
                // 序列化响应
                nlohmann::json json_response = response;
                res.set_content(json_response.dump(), "application/json");
                res.status = 200;
                
                span->SetStatus(trace::StatusCode::kOk);
                span->AddEvent("backend_call_completed");
                
            } catch (const std::exception& e) {
                // 记录异常
                span->SetStatus(trace::StatusCode::kError, e.what());
                span->AddEvent("backend_call_failed", {
                    {"exception.type", typeid(e).name()},
                    {"exception.message", e.what()}
                });
                
                nlohmann::json error_response = {
                    {"success", false},
                    {"message", e.what()}
                };
                res.set_content(error_response.dump(), "application/json");
                res.status = 500;
            }
        };
    }

    /**
     * @brief 发送TCP请求到后端服务
     * 这里关键是将当前的HTTP追踪上下文转换为TCP追踪上下文
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
            // *** 关键：获取当前追踪上下文并转换为TCP格式 ***
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
    std::string service_name_;
    std::string service_version_;
    std::string host_;
    int port_;
    std::atomic<bool> running_;
    
    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;
    
    // 后端TCP服务地址
    std::string user_service_host_;
    int user_service_port_;
    std::string message_service_host_;
    int message_service_port_;
    std::string notification_service_host_;
    int notification_service_port_;
};

#endif // TCP_GATEWAY_SERVICE_H