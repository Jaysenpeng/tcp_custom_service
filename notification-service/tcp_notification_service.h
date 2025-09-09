#ifndef TCP_NOTIFICATION_SERVICE_H
#define TCP_NOTIFICATION_SERVICE_H

#include "../common/tcp_service_base.h"
#include "../common/models.h"
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <random>
#include <chrono>

/**
 * @brief TCP通知服务类
 * 继承自TcpServiceBase，使用TCP协议和优化的上下文传播
 */
class TcpNotificationService : public TcpServiceBase {
public:
    /**
     * @brief 构造函数
     */
    TcpNotificationService(const std::string& host, int port,
                          const std::string& user_service_host, int user_service_port)
        : TcpServiceBase("notification-service", "1.0.0", host, port),
          user_service_host_(user_service_host),
          user_service_port_(user_service_port) {
        // 初始化随机数生成器
        std::random_device rd;
        random_engine_ = std::mt19937(rd());
    }

    /**
     * @brief 析构函数
     */
    ~TcpNotificationService() = default;

protected:
    /**
     * @brief 注册消息处理器
     */
    void RegisterHandlers() override {
        // 注册发送通知处理器
        RegisterHandler<chat::models::NotificationRequest, chat::models::NotificationResponse>(
            "notification.send", 
            [this](const chat::models::NotificationRequest& request) {
                return SendNotification(request);
            }
        );

        // 注册获取通知列表处理器
        RegisterHandler<chat::models::GetNotificationsRequest, chat::models::GetNotificationsResponse>(
            "notification.get",
            [this](const chat::models::GetNotificationsRequest& request) {
                return GetNotifications(request);
            }
        );
    }

private:
    /**
     * @brief 发送通知
     */
    chat::models::NotificationResponse SendNotification(const chat::models::NotificationRequest& request) {
        auto scope = CreateSpan("notification_service.send_notification");
        auto span = GetCurrentSpan();
        
        span->SetAttribute("user_id", request.user_id);
        span->SetAttribute("notification_type", request.type);
        span->SetAttribute("protocol", "tcp");
        
        chat::models::NotificationResponse response;
        
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
            
            // 生成通知ID
            std::string notification_id = GenerateUUID();
            
            // 创建通知
            span->AddEvent("creating_notification");
            chat::models::Notification notification;
            notification.notification_id = notification_id;
            notification.user_id = request.user_id;
            notification.type = request.type;
            notification.title = request.title;
            notification.content = request.content;
            notification.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            notification.is_read = false;
            
            // 存储通知
            notifications_by_id_[notification_id] = notification;
            notifications_by_user_[request.user_id].push_back(notification_id);
            
            response.success = true;
            response.message = "通知发送成功";
            response.notification_id = notification_id;
            response.timestamp = notification.timestamp;
            
            span->SetAttribute("notification_id", notification_id);
            span->SetStatus(trace::StatusCode::kOk);
            span->AddEvent("notification_sent");
            
        } catch (const std::exception& e) {
            response.success = false;
            response.message = std::string("发送通知失败: ") + e.what();
            
            span->SetStatus(trace::StatusCode::kError, e.what());
        }
        
        return response;
    }

    /**
     * @brief 获取用户通知列表
     */
    chat::models::GetNotificationsResponse GetNotifications(const chat::models::GetNotificationsRequest& request) {
        auto scope = CreateSpan("notification_service.get_notifications");
        auto span = GetCurrentSpan();
        
        span->SetAttribute("user_id", request.user_id);
        span->SetAttribute("protocol", "tcp");
        
        chat::models::GetNotificationsResponse response;
        
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
            
            // 获取用户通知ID列表
            span->AddEvent("fetching_notifications");
            auto user_it = notifications_by_user_.find(request.user_id);
            if (user_it == notifications_by_user_.end()) {
                response.success = true;
                response.total_count = 0;
                span->SetStatus(trace::StatusCode::kOk);
                return response;
            }
            
            std::vector<std::string> notification_ids = user_it->second;
            
            // 转换为通知对象
            for (const auto& notif_id : notification_ids) {
                auto notif_it = notifications_by_id_.find(notif_id);
                if (notif_it != notifications_by_id_.end()) {
                    response.notifications.push_back(notif_it->second);
                }
            }
            
            // 按时间排序（最新的在前）
            std::sort(response.notifications.begin(), response.notifications.end(),
                     [](const chat::models::Notification& a, const chat::models::Notification& b) {
                         return a.timestamp > b.timestamp;
                     });
            
            // 分页处理
            if (request.limit > 0 && response.notifications.size() > request.limit) {
                response.notifications.resize(request.limit);
            }
            
            response.success = true;
            response.total_count = static_cast<int>(response.notifications.size());
            
            span->SetAttribute("notification_count", response.total_count);
            span->SetStatus(trace::StatusCode::kOk);
            span->AddEvent("notifications_retrieved");
            
        } catch (const std::exception& e) {
            response.success = false;
            response.message = std::string("获取通知失败: ") + e.what();
            
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

    // 通知存储
    std::map<std::string, chat::models::Notification> notifications_by_id_;
    // 按用户ID存储通知ID
    std::map<std::string, std::vector<std::string>> notifications_by_user_;
    // 互斥锁
    std::mutex mutex_;
    // 随机数生成器
    std::mt19937 random_engine_;
    
    // user-service连接信息
    std::string user_service_host_;
    int user_service_port_;
};

#endif // TCP_NOTIFICATION_SERVICE_H