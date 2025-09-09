#ifndef TCP_MESSAGE_SERVICE_H
#define TCP_MESSAGE_SERVICE_H

#include "../common/tcp_service_base.h"
#include "../common/models.h"
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <random>
#include <chrono>

/**
 * @brief TCP消息服务类
 * 继承自TcpServiceBase，使用TCP协议和优化的上下文传播
 */
class TcpMessageService : public TcpServiceBase {
public:
    /**
     * @brief 构造函数
     */
    TcpMessageService(const std::string& host, int port,
                      const std::string& user_service_host, int user_service_port)
        : TcpServiceBase("message-service", "1.0.0", host, port),
          user_service_host_(user_service_host),
          user_service_port_(user_service_port) {
        // 初始化随机数生成器
        std::random_device rd;
        random_engine_ = std::mt19937(rd());
    }

    /**
     * @brief 析构函数
     */
    ~TcpMessageService() = default;

protected:
    /**
     * @brief 注册消息处理器
     */
    void RegisterHandlers() override {
        // 注册发送消息处理器
        RegisterHandler<chat::models::SendMessageRequest, chat::models::SendMessageResponse>(
            "message.send", 
            [this](const chat::models::SendMessageRequest& request) {
                return SendMessage(request);
            }
        );

        // 注册获取消息列表处理器
        RegisterHandler<chat::models::GetMessagesRequest, chat::models::GetMessagesResponse>(
            "message.get",
            [this](const chat::models::GetMessagesRequest& request) {
                return GetMessages(request);
            }
        );

        // 注册标记消息已读处理器
        RegisterHandler<chat::models::MarkMessageReadRequest, chat::models::MarkMessageReadResponse>(
            "message.mark_read",
            [this](const chat::models::MarkMessageReadRequest& request) {
                return MarkMessageRead(request);
            }
        );
    }

private:
    /**
     * @brief 发送消息
     */
    chat::models::SendMessageResponse SendMessage(const chat::models::SendMessageRequest& request) {
        auto scope = CreateSpan("message_service.send_message");
        auto span = GetCurrentSpan();
        
        span->SetAttribute("sender_id", request.sender_id);
        span->SetAttribute("receiver_id", request.receiver_id);
        span->SetAttribute("message_length", static_cast<int>(request.content.length()));
        span->SetAttribute("protocol", "tcp");
        
        chat::models::SendMessageResponse response;
        
        try {
            // 验证发送者
            span->AddEvent("validating_sender");
            if (!ValidateUser(request.sender_id)) {
                response.success = false;
                response.message = "发送者不存在";
                span->SetStatus(trace::StatusCode::kError, "发送者不存在");
                return response;
            }
            
            // 验证接收者  
            span->AddEvent("validating_receiver");
            if (!ValidateUser(request.receiver_id)) {
                response.success = false;
                response.message = "接收者不存在";
                span->SetStatus(trace::StatusCode::kError, "接收者不存在");
                return response;
            }
            
            std::unique_lock<std::mutex> lock(mutex_);
            
            // 生成消息ID
            std::string message_id = GenerateUUID();
            
            // 创建消息
            span->AddEvent("creating_message");
            chat::models::Message message;
            message.message_id = message_id;
            message.sender_id = request.sender_id;
            message.receiver_id = request.receiver_id;
            message.content = request.content;
            message.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            message.is_read = false;
            
            // 存储消息
            messages_by_id_[message_id] = message;
            
            // 更新索引
            messages_by_user_[request.sender_id].push_back(message_id);
            messages_by_user_[request.receiver_id].push_back(message_id);
            
            // 会话索引（用户对）
            auto conversation_key = std::make_pair(
                std::min(request.sender_id, request.receiver_id),
                std::max(request.sender_id, request.receiver_id)
            );
            messages_by_conversation_[conversation_key].push_back(message_id);
            
            response.success = true;
            response.message = "消息发送成功";
            response.message_id = message_id;
            response.timestamp = message.timestamp;
            
            span->SetAttribute("message_id", message_id);
            span->SetStatus(trace::StatusCode::kOk);
            span->AddEvent("message_sent");
            
        } catch (const std::exception& e) {
            response.success = false;
            response.message = std::string("发送消息失败: ") + e.what();
            
            span->SetStatus(trace::StatusCode::kError, e.what());
        }
        
        return response;
    }

    /**
     * @brief 获取消息列表
     */
    chat::models::GetMessagesResponse GetMessages(const chat::models::GetMessagesRequest& request) {
        auto scope = CreateSpan("message_service.get_messages");
        auto span = GetCurrentSpan();
        
        span->SetAttribute("user_id", request.user_id);
        span->SetAttribute("protocol", "tcp");
        if (!request.other_user_id.empty()) {
            span->SetAttribute("other_user_id", request.other_user_id);
        }
        
        chat::models::GetMessagesResponse response;
        
        try {
            // 验证用户
            span->AddEvent("validating_user");
            if (!ValidateUser(request.user_id)) {
                response.success = false;
                response.message = "用户不存在";
                span->SetStatus(trace::StatusCode::kError, "用户不存在");
                return response;
            }
            
            std::unique_lock<std::mutex> lock(mutex_);
            
            std::vector<std::string> message_ids;
            
            if (!request.other_user_id.empty()) {
                // 获取与特定用户的会话消息
                span->AddEvent("fetching_conversation_messages");
                auto conversation_key = std::make_pair(
                    std::min(request.user_id, request.other_user_id),
                    std::max(request.user_id, request.other_user_id)
                );
                
                auto conv_it = messages_by_conversation_.find(conversation_key);
                if (conv_it != messages_by_conversation_.end()) {
                    message_ids = conv_it->second;
                }
            } else {
                // 获取用户所有相关消息
                span->AddEvent("fetching_all_messages");
                auto user_it = messages_by_user_.find(request.user_id);
                if (user_it != messages_by_user_.end()) {
                    message_ids = user_it->second;
                }
            }
            
            // 转换为消息对象
            for (const auto& msg_id : message_ids) {
                auto msg_it = messages_by_id_.find(msg_id);
                if (msg_it != messages_by_id_.end()) {
                    response.messages.push_back(msg_it->second);
                }
            }
            
            // 按时间排序（最新的在前）
            std::sort(response.messages.begin(), response.messages.end(),
                     [](const chat::models::Message& a, const chat::models::Message& b) {
                         return a.timestamp > b.timestamp;
                     });
            
            // 分页处理
            if (request.limit > 0 && response.messages.size() > request.limit) {
                response.messages.resize(request.limit);
            }
            
            response.success = true;
            response.total_count = static_cast<int>(response.messages.size());
            
            span->SetAttribute("message_count", response.total_count);
            span->SetStatus(trace::StatusCode::kOk);
            span->AddEvent("messages_retrieved");
            
        } catch (const std::exception& e) {
            response.success = false;
            response.message = std::string("获取消息失败: ") + e.what();
            
            span->SetStatus(trace::StatusCode::kError, e.what());
        }
        
        return response;
    }

    /**
     * @brief 标记消息已读
     */
    chat::models::MarkMessageReadResponse MarkMessageRead(const chat::models::MarkMessageReadRequest& request) {
        auto scope = CreateSpan("message_service.mark_read");
        auto span = GetCurrentSpan();
        
        span->SetAttribute("user_id", request.user_id);
        span->SetAttribute("message_id", request.message_id);
        span->SetAttribute("protocol", "tcp");
        
        chat::models::MarkMessageReadResponse response;
        
        try {
            std::unique_lock<std::mutex> lock(mutex_);
            
            auto msg_it = messages_by_id_.find(request.message_id);
            if (msg_it == messages_by_id_.end()) {
                response.success = false;
                response.message = "消息不存在";
                span->SetStatus(trace::StatusCode::kError, "消息不存在");
                return response;
            }
            
            chat::models::Message& message = msg_it->second;
            
            // 检查权限（只有接收者可以标记已读）
            if (message.receiver_id != request.user_id) {
                response.success = false;
                response.message = "无权限标记此消息";
                span->SetStatus(trace::StatusCode::kError, "权限不足");
                return response;
            }
            
            // 标记已读
            message.is_read = true;
            
            response.success = true;
            response.message = "消息已标记为已读";
            
            span->SetStatus(trace::StatusCode::kOk);
            span->AddEvent("message_marked_read");
            
        } catch (const std::exception& e) {
            response.success = false;
            response.message = std::string("标记已读失败: ") + e.what();
            
            span->SetStatus(trace::StatusCode::kError, e.what());
        }
        
        return response;
    }

    /**
     * @brief 验证用户是否存在（通过TCP调用user-service）
     */
    bool ValidateUser(const std::string& user_id) {
        try {
            // 构造获取用户请求
            chat::models::GetUserRequest request;
            request.user_id = user_id;
            
            // 通过TCP调用user-service
            auto response = SendTcpRequest<chat::models::GetUserRequest, chat::models::UserInfo>(
                user_service_host_, user_service_port_, "user.get", request
            );
            
            return response.success;
            
        } catch (const std::exception& e) {
            std::cerr << "验证用户失败: " << e.what() << std::endl;
            return false;
        }
    }

    /**
     * @brief 生成UUID
     */
    std::string GenerateUUID() {
        std::uniform_int_distribution<> dis(0, 15);
        std::stringstream ss;
        ss << std::hex;
        
        for (int i = 0; i < 32; ++i) {
            if (i == 8 || i == 12 || i == 16 || i == 20) {
                ss << "-";
            }
            ss << dis(random_engine_);
        }
        
        return ss.str();
    }

    // 消息存储
    std::map<std::string, chat::models::Message> messages_by_id_;
    // 按用户ID存储收发的消息ID
    std::map<std::string, std::vector<std::string>> messages_by_user_;
    // 按会话存储消息ID（用户ID对）
    std::map<std::pair<std::string, std::string>, std::vector<std::string>> messages_by_conversation_;
    // 互斥锁
    std::mutex mutex_;
    // 随机数生成器
    std::mt19937 random_engine_;
    
    // user-service连接信息
    std::string user_service_host_;
    int user_service_port_;
};

#endif // TCP_MESSAGE_SERVICE_H