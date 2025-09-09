#ifndef MODELS_H
#define MODELS_H

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace chat {
namespace models {

// 用户注册请求
struct RegisterRequest {
    std::string username;
    std::string password;
    std::string email;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(RegisterRequest, username, password, email)
};

// 用户注册响应
struct RegisterResponse {
    bool success;
    std::string message;
    std::string user_id;
    std::string token;  // 添加token字段
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(RegisterResponse, success, message, user_id, token)
};

// 用户登录请求
struct LoginRequest {
    std::string username;
    std::string password;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LoginRequest, username, password)
};

// 用户登录响应
struct LoginResponse {
    bool success;
    std::string message;
    std::string token;
    std::string user_id;
    std::string username;  // 添加username字段
    std::string email;     // 添加email字段
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(LoginResponse, success, message, token, user_id, username, email)
};

// 获取用户信息请求
struct GetUserRequest {
    std::string user_id;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(GetUserRequest, user_id)
};

// 用户信息
struct UserInfo {
    bool success = true;        // 添加success字段
    std::string message;        // 添加message字段
    std::string user_id;
    std::string username;
    std::string email;
    std::string status;
    int64_t created_at;
    int64_t last_active;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UserInfo, success, message, user_id, username, email, status, created_at, last_active)
};

// 消息发送请求
struct SendMessageRequest {
    std::string sender_id;
    std::string receiver_id;
    std::string content;
    std::string message_type;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SendMessageRequest, sender_id, receiver_id, content, message_type)
};

// 消息发送响应
struct SendMessageResponse {
    bool success;
    std::string message;
    std::string message_id;
    int64_t timestamp;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(SendMessageResponse, success, message, message_id, timestamp)
};

// 获取消息请求
struct GetMessagesRequest {
    std::string user_id;
    std::string other_user_id;
    int32_t limit;
    int64_t before_timestamp;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(GetMessagesRequest, user_id, other_user_id, limit, before_timestamp)
};

// 消息对象
struct Message {
    std::string message_id;
    std::string sender_id;
    std::string receiver_id;
    std::string content;
    std::string message_type;
    bool is_read = false;  // 修改字段名并添加默认值
    int64_t timestamp;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Message, message_id, sender_id, receiver_id, content, message_type, is_read, timestamp)
};

// 获取消息响应
struct GetMessagesResponse {
    bool success = true;     // 添加success字段
    std::string message;     // 添加message字段
    std::vector<Message> messages;
    bool has_more;
    int total_count = 0;     // 添加total_count字段
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(GetMessagesResponse, success, message, messages, has_more, total_count)
};

// 标记消息已读请求
struct MarkMessageReadRequest {
    std::string user_id;
    std::string message_id;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(MarkMessageReadRequest, user_id, message_id)
};

// 标记消息已读响应
struct MarkMessageReadResponse {
    bool success;
    std::string message;  // 添加message字段
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(MarkMessageReadResponse, success, message)
};

// 通知请求
struct NotificationRequest {
    std::string user_id;
    std::string title;
    std::string content;
    std::string type;  // 修改字段名为type
    std::map<std::string, std::string> metadata;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(NotificationRequest, user_id, title, content, type, metadata)
};

// 通知响应
struct NotificationResponse {
    bool success;
    std::string message;          // 添加message字段
    std::string notification_id;
    int64_t timestamp;           // 添加timestamp字段
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(NotificationResponse, success, message, notification_id, timestamp)
};

// 获取通知列表请求
struct GetNotificationsRequest {
    std::string user_id;
    int32_t limit;
    int64_t before_timestamp;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(GetNotificationsRequest, user_id, limit, before_timestamp)
};

// 通知对象
struct Notification {
    std::string notification_id;
    std::string user_id;
    std::string title;
    std::string content;
    std::string type;        // 修改字段名为type
    bool is_read = false;    // 修改字段名为is_read并添加默认值
    int64_t timestamp;
    std::map<std::string, std::string> metadata;
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Notification, notification_id, user_id, title, content, type, is_read, timestamp, metadata)
};

// 获取通知列表响应
struct GetNotificationsResponse {
    bool success = true;     // 添加success字段
    std::string message;     // 添加message字段
    std::vector<Notification> notifications;
    bool has_more;
    int total_count = 0;     // 添加total_count字段
    
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(GetNotificationsResponse, success, message, notifications, has_more, total_count)
};

} // namespace models
} // namespace chat

#endif // MODELS_H