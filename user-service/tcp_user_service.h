#ifndef TCP_USER_SERVICE_H
#define TCP_USER_SERVICE_H

#include "../common/tcp_service_base.h"
#include "../common/models.h"
#include <string>
#include <map>
#include <mutex>
#include <random>
#include <chrono>

/**
 * @brief TCP用户服务类
 * 继承自TcpServiceBase，使用TCP协议和优化的上下文传播
 */
class TcpUserService : public TcpServiceBase {
public:
    /**
     * @brief 构造函数
     */
    TcpUserService(const std::string& host, int port)
        : TcpServiceBase("user-service", "1.0.0", host, port) {
        // 初始化随机数生成器
        std::random_device rd;
        random_engine_ = std::mt19937(rd());
    }

    /**
     * @brief 析构函数
     */
    ~TcpUserService() = default;

protected:
    /**
     * @brief 注册消息处理器
     */
    void RegisterHandlers() override {
        // 注册用户注册处理器
        RegisterHandler<chat::models::RegisterRequest, chat::models::RegisterResponse>(
            "user.register", 
            [this](const chat::models::RegisterRequest& request) {
                return Register(request);
            }
        );

        // 注册用户登录处理器
        RegisterHandler<chat::models::LoginRequest, chat::models::LoginResponse>(
            "user.login",
            [this](const chat::models::LoginRequest& request) {
                return Login(request);
            }
        );

        // 注册获取用户信息处理器
        RegisterHandler<chat::models::GetUserRequest, chat::models::UserInfo>(
            "user.get",
            [this](const chat::models::GetUserRequest& request) {
                return GetUser(request.user_id);
            }
        );
    }

private:
    /**
     * @brief 用户注册
     */
    chat::models::RegisterResponse Register(const chat::models::RegisterRequest& request) {
        // 创建一个有范围的span
        auto scope = CreateSpan("user_service.register");
        auto span = GetCurrentSpan();
        
        span->SetAttribute("username", request.username);
        span->SetAttribute("email", request.email);
        span->SetAttribute("protocol", "tcp");
        
        // 在关键操作前添加一个事件
        span->AddEvent("validating_registration");
        
        chat::models::RegisterResponse response;
        
        try {
            std::unique_lock<std::mutex> lock(mutex_);
            
            // 检查用户名是否存在
            if (users_by_username_.find(request.username) != users_by_username_.end()) {
                response.success = false;
                response.message = "用户名已存在";
                
                // 记录失败状态
                span->SetStatus(trace::StatusCode::kError, "用户名已存在");
                return response;
            }
            
            // 生成用户ID
            std::string user_id = GenerateUUID();
            
            // 记录用户注册
            span->AddEvent("creating_user_record");
            
            // 创建用户数据
            UserData user;
            user.user_id = user_id;
            user.username = request.username;
            user.email = request.email;
            user.password = request.password; // 实际应用中应该哈希密码
            user.status = "active";
            user.token = GenerateAuthToken();
            user.created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            user.last_active = user.created_at;
            
            // 存储用户数据
            users_by_id_[user_id] = user;
            users_by_username_[request.username] = user_id;
            
            // 构造响应
            response.success = true;
            response.message = "注册成功";
            response.user_id = user_id;
            response.token = user.token;
            
            span->SetAttribute("user_id", user_id);
            span->SetStatus(trace::StatusCode::kOk);
            span->AddEvent("user_registered");
            
        } catch (const std::exception& e) {
            response.success = false;
            response.message = std::string("注册失败: ") + e.what();
            
            span->SetStatus(trace::StatusCode::kError, e.what());
            span->AddEvent("registration_failed", {
                {"error", e.what()}
            });
        }
        
        return response;
    }

    /**
     * @brief 用户登录
     */
    chat::models::LoginResponse Login(const chat::models::LoginRequest& request) {
        // 创建一个有范围的span
        auto scope = CreateSpan("user_service.login");
        auto span = GetCurrentSpan();
        
        span->SetAttribute("username", request.username);
        span->SetAttribute("protocol", "tcp");
        span->AddEvent("validating_credentials");
        
        chat::models::LoginResponse response;
        
        try {
            std::unique_lock<std::mutex> lock(mutex_);
            
            // 查找用户
            auto username_it = users_by_username_.find(request.username);
            if (username_it == users_by_username_.end()) {
                response.success = false;
                response.message = "用户不存在";
                
                span->SetStatus(trace::StatusCode::kError, "用户不存在");
                return response;
            }
            
            std::string user_id = username_it->second;
            auto user_it = users_by_id_.find(user_id);
            if (user_it == users_by_id_.end()) {
                response.success = false;
                response.message = "用户数据不一致";
                
                span->SetStatus(trace::StatusCode::kError, "用户数据不一致");
                return response;
            }
            
            UserData& user = user_it->second;
            
            // 验证密码
            if (user.password != request.password) {
                response.success = false;
                response.message = "密码错误";
                
                span->SetStatus(trace::StatusCode::kError, "密码错误");
                span->AddEvent("authentication_failed");
                return response;
            }
            
            // 更新最后活跃时间
            user.last_active = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            // 构造响应
            response.success = true;
            response.message = "登录成功";
            response.user_id = user.user_id;
            response.token = user.token;
            response.username = user.username;
            response.email = user.email;
            
            span->SetAttribute("user_id", user.user_id);
            span->SetStatus(trace::StatusCode::kOk);
            span->AddEvent("user_authenticated");
            
        } catch (const std::exception& e) {
            response.success = false;
            response.message = std::string("登录失败: ") + e.what();
            
            span->SetStatus(trace::StatusCode::kError, e.what());
        }
        
        return response;
    }

    /**
     * @brief 获取用户信息
     */
    chat::models::UserInfo GetUser(const std::string& user_id) {
        // 创建一个有范围的span
        auto scope = CreateSpan("user_service.get_user");
        auto span = GetCurrentSpan();
        
        span->SetAttribute("user_id", user_id);
        span->SetAttribute("protocol", "tcp");
        
        chat::models::UserInfo userInfo;
        
        try {
            std::unique_lock<std::mutex> lock(mutex_);
            
            auto it = users_by_id_.find(user_id);
            if (it == users_by_id_.end()) {
                userInfo.success = false;
                userInfo.message = "用户不存在";
                
                span->SetStatus(trace::StatusCode::kError, "用户不存在");
                return userInfo;
            }
            
            const UserData& user = it->second;
            
            // 构造用户信息
            userInfo.success = true;
            userInfo.user_id = user.user_id;
            userInfo.username = user.username;
            userInfo.email = user.email;
            userInfo.status = user.status;
            userInfo.created_at = user.created_at;
            userInfo.last_active = user.last_active;
            
            span->SetStatus(trace::StatusCode::kOk);
            span->AddEvent("user_info_retrieved");
            
        } catch (const std::exception& e) {
            userInfo.success = false;
            userInfo.message = std::string("获取用户信息失败: ") + e.what();
            
            span->SetStatus(trace::StatusCode::kError, e.what());
        }
        
        return userInfo;
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

    /**
     * @brief 生成认证令牌
     */
    std::string GenerateAuthToken() {
        std::uniform_int_distribution<> dis(0, 61);
        const std::string chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        
        std::string token;
        for (int i = 0; i < 32; ++i) {
            token += chars[dis(random_engine_)];
        }
        
        return token;
    }

    // 用户数据结构
    struct UserData {
        std::string user_id;
        std::string username;
        std::string email;
        std::string password;
        std::string status;
        std::string token;
        int64_t created_at;
        int64_t last_active;
    };

    std::map<std::string, UserData> users_by_id_;        // 按ID索引用户
    std::map<std::string, std::string> users_by_username_; // 按用户名索引用户ID
    std::mutex mutex_;                                  // 保护用户数据的互斥锁
    std::mt19937 random_engine_;                        // 随机数生成器
};

#endif // TCP_USER_SERVICE_H