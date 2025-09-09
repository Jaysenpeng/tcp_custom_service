#ifndef CONTEXT_PROPAGATION_H
#define CONTEXT_PROPAGATION_H

#include <map>
#include <string>
#include <functional>
#include "opentelemetry/trace/context.h"
#include "opentelemetry/context/propagation/text_map_propagator.h"
#include "opentelemetry/context/propagation/composite_propagator.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"
#include "opentelemetry/trace/propagation/b3_propagator.h"
#include "opentelemetry/context/propagation/global_propagator.h"
#include "../third_party/httplib.h"

namespace context_propagation {

using namespace opentelemetry;

/**
 * @brief HTTP客户端头部载体
 */
class HttpClientCarrier : public context::propagation::TextMapCarrier {
public:
    explicit HttpClientCarrier(httplib::Headers& headers) : headers_(headers) {}

    HttpClientCarrier() = default;
    ~HttpClientCarrier() = default;

    void Set(nostd::string_view key, nostd::string_view value) noexcept override {
        headers_.emplace(std::string(key), std::string(value));
    }

    // 以下方法不会被调用，但为了满足接口需要提供
    nostd::string_view Get(nostd::string_view key) const noexcept override {
        return ""; // 客户端注入时不需要读取
    }

    bool Keys(std::function<bool(nostd::string_view)> callback) const noexcept {
        return true; // 客户端注入时不需要遍历键
    }

private:
    httplib::Headers& headers_;
};

/**
 * @brief HTTP服务端头部载体
 */
class HttpServerCarrier : public context::propagation::TextMapCarrier {
public:
    explicit HttpServerCarrier(const httplib::Request& request) {
        // 从HTTP头部中提取所有键值对
        for (const auto& header : request.headers) {
            headers_[header.first] = header.second;
        }
    }

    HttpServerCarrier() = default;
    ~HttpServerCarrier() = default;

    nostd::string_view Get(nostd::string_view key) const noexcept override {
        auto it = headers_.find(std::string(key));
        if (it != headers_.end()) {
            return nostd::string_view(it->second);
        }
        return {};
    }

    bool Keys(std::function<bool(nostd::string_view)> callback) const noexcept {
        for (const auto& kv : headers_) {
            if (!callback(nostd::string_view(kv.first))) {
                return false;
            }
        }
        return true;
    }

    // 以下方法不会被调用，但为了满足接口需要提供
    void Set(nostd::string_view key, nostd::string_view value) noexcept override {
        // 服务端提取时不需要写入
    }

private:
    std::map<std::string, std::string> headers_;
};

/**
 * @brief 注入当前活跃的上下文到HTTP客户端头部
 * @param headers HTTP客户端头部
 */
inline void InjectHttpContext(httplib::Headers& headers) {
    auto current_ctx = context::RuntimeContext::GetCurrent();
    auto propagator = context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
    
    HttpClientCarrier carrier(headers);
    propagator->Inject(carrier, current_ctx);
}

/**
 * @brief 从HTTP请求中提取并应用远程上下文
 * @param request HTTP请求
 * @return 包含远程上下文的作用域对象，离开作用域时自动恢复原上下文
 */
inline nostd::unique_ptr<context::Token> ExtractHttpContext(const httplib::Request& request) {
    HttpServerCarrier carrier(request);
    auto propagator = context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
    
    auto current_ctx = context::RuntimeContext::GetCurrent();
    auto new_context = propagator->Extract(carrier, current_ctx);
    
    return context::RuntimeContext::Attach(new_context);
}

} // namespace context_propagation

#endif // CONTEXT_PROPAGATION_H 