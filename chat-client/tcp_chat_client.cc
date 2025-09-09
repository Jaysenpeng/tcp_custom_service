#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <thread>
#include <map>
#include <vector>
#include <functional>

#include "../third_party/httplib.h"
#include "../common/telemetry.h"
#include "../common/context_propagation.h"
#include "../common/models.h"

/**
 * @brief TCP聊天客户端类
 * 通过HTTP连接到TCP API网关，支持分布式追踪
 */
class TcpChatClient {
public:
    /**
     * @brief 构造函数
     * @param gateway_host API网关主机
     * @param gateway_port API网关端口
     */
    TcpChatClient(const std::string& gateway_host, int gateway_port) 
        : gateway_host_(gateway_host), gateway_port_(gateway_port) {
        
        // 创建HTTP客户端
        client_ = std::make_unique<httplib::Client>(gateway_host, gateway_port);
        
        // 初始化OpenTelemetry
        Telemetry::InitTelemetry("chat-client", "1.0.0");
        
        std::cout << "已连接到TCP API网关: " << gateway_host << ":" << gateway_port << std::endl;
    }
    
    /**
     * @brief 析构函数
     */
    ~TcpChatClient() {
        Telemetry::CleanupTelemetry();
    }

    /**
     * @brief 运行聊天客户端
     */
    void Run() {
        std::map<std::string, std::function<void()>> commands = {
            { "register", [this](){ RegisterUser(); } },
            { "login", [this](){ LoginUser(); } },
            { "send", [this](){ SendMessage(); } },
            { "get-messages", [this](){ GetMessages(); } },
            { "send-notification", [this](){ SendNotification(); } },
            { "get-notifications", [this](){ GetNotifications(); } },
            { "help", [this](){ ShowHelp(); } },
            { "quit", [this](){ quit_ = true; } }
        };

        std::cout << "\n=== TCP 聊天客户端 v1.0.0 ===" << std::endl;
        std::cout << "连接到TCP后端的分布式聊天系统" << std::endl;
        std::cout << "输入 'help' 查看可用命令" << std::endl;

        quit_ = false;
        while (!quit_) {
            std::cout << "\n> ";
            std::string command;
            std::getline(std::cin, command);

            if (command.empty()) {
                continue;
            }

            auto it = commands.find(command);
            if (it != commands.end()) {
                try {
                    it->second();
                } catch (const std::exception& e) {
                    std::cerr << "执行命令时出错: " << e.what() << std::endl;
                }
            } else {
                std::cout << "未知命令: " << command << ". 输入 'help' 查看可用命令." << std::endl;
            }
        }

        std::cout << "感谢使用TCP聊天客户端!" << std::endl;
    }

private:
    /**
     * @brief 显示帮助信息
     */
    void ShowHelp() {
        std::cout << "\n可用命令:" << std::endl;
        std::cout << "  register         - 注册新用户" << std::endl;
        std::cout << "  login            - 用户登录" << std::endl;
        std::cout << "  send             - 发送消息" << std::endl;
        std::cout << "  get-messages     - 获取消息列表" << std::endl;
        std::cout << "  send-notification - 发送通知" << std::endl;
        std::cout << "  get-notifications - 获取通知列表" << std::endl;
        std::cout << "  help             - 显示此帮助信息" << std::endl;
        std::cout << "  quit             - 退出客户端" << std::endl;
    }

    /**
     * @brief 用户注册
     */
    void RegisterUser() {
        auto scope = CreateSpan("client.register_user");
        auto span = GetCurrentSpan();
        
        span->SetAttribute("operation", "register");
        span->SetAttribute("client.type", "tcp_chat_client");
        
        chat::models::RegisterRequest request;
        
        std::cout << "=== 用户注册 ===" << std::endl;
        std::cout << "用户名: ";
        std::getline(std::cin, request.username);
        
        std::cout << "邮箱: ";
        std::getline(std::cin, request.email);
        
        std::cout << "密码: ";
        std::getline(std::cin, request.password);
        
        span->SetAttribute("username", request.username);
        span->SetAttribute("email", request.email);
        
        try {
            span->AddEvent("sending_register_request");
            auto response = SendHttpRequest<chat::models::RegisterRequest, chat::models::RegisterResponse>(
                "POST", "/api/users/register", request
            );
            
            if (response.success) {
                std::cout << "注册成功!" << std::endl;
                std::cout << "用户ID: " << response.user_id << std::endl;
                std::cout << "令牌: " << response.token << std::endl;
                
                // 保存登录信息
                current_user_id_ = response.user_id;
                current_token_ = response.token;
                
                span->SetAttribute("user_id", response.user_id);
                span->SetStatus(trace::StatusCode::kOk);
                span->AddEvent("registration_successful");
            } else {
                std::cout << "注册失败: " << response.message << std::endl;
                span->SetStatus(trace::StatusCode::kError, response.message);
            }
        } catch (const std::exception& e) {
            std::cout << "注册时出错: " << e.what() << std::endl;
            span->SetStatus(trace::StatusCode::kError, e.what());
        }
    }

    /**
     * @brief 用户登录
     */
    void LoginUser() {
        auto scope = CreateSpan("client.login_user");
        auto span = GetCurrentSpan();
        
        span->SetAttribute("operation", "login");
        span->SetAttribute("client.type", "tcp_chat_client");
        
        chat::models::LoginRequest request;
        
        std::cout << "=== 用户登录 ===" << std::endl;
        std::cout << "用户名: ";
        std::getline(std::cin, request.username);
        
        std::cout << "密码: ";
        std::getline(std::cin, request.password);
        
        span->SetAttribute("username", request.username);
        
        try {
            span->AddEvent("sending_login_request");
            auto response = SendHttpRequest<chat::models::LoginRequest, chat::models::LoginResponse>(
                "POST", "/api/users/login", request
            );
            
            if (response.success) {
                std::cout << "登录成功!" << std::endl;
                std::cout << "欢迎, " << response.username << "!" << std::endl;
                
                // 保存登录信息
                current_user_id_ = response.user_id;
                current_token_ = response.token;
                current_username_ = response.username;
                
                span->SetAttribute("user_id", response.user_id);
                span->SetStatus(trace::StatusCode::kOk);
                span->AddEvent("login_successful");
            } else {
                std::cout << "登录失败: " << response.message << std::endl;
                span->SetStatus(trace::StatusCode::kError, response.message);
            }
        } catch (const std::exception& e) {
            std::cout << "登录时出错: " << e.what() << std::endl;
            span->SetStatus(trace::StatusCode::kError, e.what());
        }
    }

    /**
     * @brief 发送消息
     */
    void SendMessage() {
        if (!CheckLogin()) return;
        
        auto scope = CreateSpan("client.send_message");
        auto span = GetCurrentSpan();
        
        span->SetAttribute("operation", "send_message");
        span->SetAttribute("client.type", "tcp_chat_client");
        span->SetAttribute("sender_id", current_user_id_);
        
        chat::models::SendMessageRequest request;
        request.sender_id = current_user_id_;
        
        std::cout << "=== 发送消息 ===" << std::endl;
        std::cout << "接收者ID: ";
        std::getline(std::cin, request.receiver_id);
        
        std::cout << "消息内容: ";
        std::getline(std::cin, request.content);
        
        span->SetAttribute("receiver_id", request.receiver_id);
        span->SetAttribute("message_length", static_cast<int>(request.content.length()));
        
        try {
            span->AddEvent("sending_message_request");
            auto response = SendHttpRequest<chat::models::SendMessageRequest, chat::models::SendMessageResponse>(
                "POST", "/api/messages/send", request
            );
            
            if (response.success) {
                std::cout << "消息发送成功!" << std::endl;
                std::cout << "消息ID: " << response.message_id << std::endl;
                
                span->SetAttribute("message_id", response.message_id);
                span->SetStatus(trace::StatusCode::kOk);
                span->AddEvent("message_sent");
            } else {
                std::cout << "发送失败: " << response.message << std::endl;
                span->SetStatus(trace::StatusCode::kError, response.message);
            }
        } catch (const std::exception& e) {
            std::cout << "发送消息时出错: " << e.what() << std::endl;
            span->SetStatus(trace::StatusCode::kError, e.what());
        }
    }

    /**
     * @brief 获取消息列表
     */
    void GetMessages() {
        if (!CheckLogin()) return;
        
        auto scope = CreateSpan("client.get_messages");
        auto span = GetCurrentSpan();
        
        span->SetAttribute("operation", "get_messages");
        span->SetAttribute("client.type", "tcp_chat_client");
        span->SetAttribute("user_id", current_user_id_);
        
        std::cout << "=== 获取消息 ===" << std::endl;
        std::cout << "对方用户ID (留空获取所有消息): ";
        std::string other_user_id;
        std::getline(std::cin, other_user_id);
        
        std::string url = "/api/messages?user_id=" + current_user_id_;
        if (!other_user_id.empty()) {
            url += "&other_user_id=" + other_user_id;
            span->SetAttribute("other_user_id", other_user_id);
        }
        url += "&limit=10";
        
        try {
            span->AddEvent("fetching_messages");
            auto response = SendHttpGetRequest<chat::models::GetMessagesResponse>(url);
            
            if (response.success) {
                std::cout << "共找到 " << response.total_count << " 条消息:" << std::endl;
                
                for (const auto& message : response.messages) {
                    auto timestamp = std::chrono::milliseconds(message.timestamp);
                    auto time_t = std::chrono::system_clock::to_time_t(
                        std::chrono::system_clock::time_point(timestamp));
                    
                    std::cout << "---" << std::endl;
                    std::cout << "ID: " << message.message_id << std::endl;
                    std::cout << "发送者: " << message.sender_id << std::endl;
                    std::cout << "接收者: " << message.receiver_id << std::endl;
                    std::cout << "内容: " << message.content << std::endl;
                    std::cout << "时间: " << std::ctime(&time_t);
                    std::cout << "已读: " << (message.is_read ? "是" : "否") << std::endl;
                }
                
                span->SetAttribute("message_count", response.total_count);
                span->SetStatus(trace::StatusCode::kOk);
                span->AddEvent("messages_retrieved");
            } else {
                std::cout << "获取消息失败: " << response.message << std::endl;
                span->SetStatus(trace::StatusCode::kError, response.message);
            }
        } catch (const std::exception& e) {
            std::cout << "获取消息时出错: " << e.what() << std::endl;
            span->SetStatus(trace::StatusCode::kError, e.what());
        }
    }

    /**
     * @brief 发送通知
     */
    void SendNotification() {
        if (!CheckLogin()) return;
        
        auto scope = CreateSpan("client.send_notification");
        auto span = GetCurrentSpan();
        
        span->SetAttribute("operation", "send_notification");
        span->SetAttribute("client.type", "tcp_chat_client");
        
        chat::models::NotificationRequest request;
        
        std::cout << "=== 发送通知 ===" << std::endl;
        std::cout << "目标用户ID: ";
        std::getline(std::cin, request.user_id);
        
        std::cout << "通知类型: ";
        std::getline(std::cin, request.type);
        
        std::cout << "标题: ";
        std::getline(std::cin, request.title);
        
        std::cout << "内容: ";
        std::getline(std::cin, request.content);
        
        span->SetAttribute("target_user_id", request.user_id);
        span->SetAttribute("notification_type", request.type);
        
        try {
            span->AddEvent("sending_notification_request");
            auto response = SendHttpRequest<chat::models::NotificationRequest, chat::models::NotificationResponse>(
                "POST", "/api/notifications/send", request
            );
            
            if (response.success) {
                std::cout << "通知发送成功!" << std::endl;
                std::cout << "通知ID: " << response.notification_id << std::endl;
                
                span->SetAttribute("notification_id", response.notification_id);
                span->SetStatus(trace::StatusCode::kOk);
                span->AddEvent("notification_sent");
            } else {
                std::cout << "发送失败: " << response.message << std::endl;
                span->SetStatus(trace::StatusCode::kError, response.message);
            }
        } catch (const std::exception& e) {
            std::cout << "发送通知时出错: " << e.what() << std::endl;
            span->SetStatus(trace::StatusCode::kError, e.what());
        }
    }

    /**
     * @brief 获取通知列表
     */
    void GetNotifications() {
        if (!CheckLogin()) return;
        
        auto scope = CreateSpan("client.get_notifications");
        auto span = GetCurrentSpan();
        
        span->SetAttribute("operation", "get_notifications");
        span->SetAttribute("client.type", "tcp_chat_client");
        span->SetAttribute("user_id", current_user_id_);
        
        std::string url = "/api/notifications?user_id=" + current_user_id_ + "&limit=10";
        
        try {
            span->AddEvent("fetching_notifications");
            auto response = SendHttpGetRequest<chat::models::GetNotificationsResponse>(url);
            
            if (response.success) {
                std::cout << "共有 " << response.total_count << " 条通知:" << std::endl;
                
                for (const auto& notification : response.notifications) {
                    auto timestamp = std::chrono::milliseconds(notification.timestamp);
                    auto time_t = std::chrono::system_clock::to_time_t(
                        std::chrono::system_clock::time_point(timestamp));
                    
                    std::cout << "---" << std::endl;
                    std::cout << "ID: " << notification.notification_id << std::endl;
                    std::cout << "类型: " << notification.type << std::endl;
                    std::cout << "标题: " << notification.title << std::endl;
                    std::cout << "内容: " << notification.content << std::endl;
                    std::cout << "时间: " << std::ctime(&time_t);
                    std::cout << "已读: " << (notification.is_read ? "是" : "否") << std::endl;
                }
                
                span->SetAttribute("notification_count", response.total_count);
                span->SetStatus(trace::StatusCode::kOk);
                span->AddEvent("notifications_retrieved");
            } else {
                std::cout << "获取通知失败: " << response.message << std::endl;
                span->SetStatus(trace::StatusCode::kError, response.message);
            }
        } catch (const std::exception& e) {
            std::cout << "获取通知时出错: " << e.what() << std::endl;
            span->SetStatus(trace::StatusCode::kError, e.what());
        }
    }

    /**
     * @brief 检查是否已登录
     */
    bool CheckLogin() {
        if (current_user_id_.empty()) {
            std::cout << "请先登录!" << std::endl;
            return false;
        }
        return true;
    }

    /**
     * @brief 发送HTTP请求
     */
    template<typename RequestType, typename ResponseType>
    ResponseType SendHttpRequest(const std::string& method, const std::string& path, const RequestType& request) {
        // 序列化请求
        nlohmann::json json_request = request;
        std::string body = json_request.dump();
        
        // 设置请求头，包含追踪信息
        httplib::Headers headers;
        context_propagation::InjectHttpContext(headers);
        headers.emplace("Content-Type", "application/json");
        
        // 发送请求
        auto result = client_->Post(path.c_str(), headers, body, "application/json");
        
        if (!result) {
            throw std::runtime_error("HTTP请求失败");
        }
        
        if (result->status != 200) {
            throw std::runtime_error("HTTP错误: " + std::to_string(result->status));
        }
        
        // 解析响应
        auto json_response = nlohmann::json::parse(result->body);
        return json_response.get<ResponseType>();
    }

    /**
     * @brief 发送HTTP GET请求
     */
    template<typename ResponseType>
    ResponseType SendHttpGetRequest(const std::string& path) {
        // 设置请求头，包含追踪信息
        httplib::Headers headers;
        context_propagation::InjectHttpContext(headers);
        
        // 发送请求
        auto result = client_->Get(path.c_str(), headers);
        
        if (!result) {
            throw std::runtime_error("HTTP请求失败");
        }
        
        if (result->status != 200) {
            throw std::runtime_error("HTTP错误: " + std::to_string(result->status));
        }
        
        // 解析响应
        auto json_response = nlohmann::json::parse(result->body);
        return json_response.get<ResponseType>();
    }

private:
    std::string gateway_host_;
    int gateway_port_;
    std::unique_ptr<httplib::Client> client_;
    
    // 当前用户信息
    std::string current_user_id_;
    std::string current_token_;
    std::string current_username_;
    
    bool quit_ = false;
};

int main(int argc, char* argv[]) {
    try {
        // 解析命令行参数
        std::string gateway_host = "127.0.0.1";
        int gateway_port = 8080;
        
        if (argc >= 2) {
            gateway_host = argv[1];
        }
        if (argc >= 3) {
            gateway_port = std::stoi(argv[2]);
        }
        
        std::cout << "连接到TCP API网关: " << gateway_host << ":" << gateway_port << std::endl;
        
        // 创建和运行客户端
        TcpChatClient client(gateway_host, gateway_port);
        client.Run();
        
    } catch (const std::exception& e) {
        std::cerr << "客户端错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}