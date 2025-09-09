#ifndef CHAT_SERVICE_TELEMETRY_H
#define CHAT_SERVICE_TELEMETRY_H

#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/context/propagation/text_map_propagator.h"
#include "opentelemetry/exporters/zipkin/zipkin_exporter_factory.h"
#include "opentelemetry/exporters/zipkin/zipkin_exporter_options.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/semconv/service_attributes.h"
#include "opentelemetry/semconv/incubating/host_attributes.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/context/runtime_context.h"
#include <curl/curl.h>
#include <unistd.h>

#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include <mutex>
#include <iostream>

namespace trace     = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace zipkin    = opentelemetry::exporter::zipkin;
namespace nostd     = opentelemetry::nostd;
namespace context   = opentelemetry::context;
namespace resource  = opentelemetry::sdk::resource;
namespace propagation = opentelemetry::context::propagation;
namespace service   = opentelemetry::semconv::service;
namespace host      = opentelemetry::semconv::host;

/**
 * @brief CURL初始化管理类，确保线程安全
 */
class CurlInitializer {
public:
    static void Initialize() {
        static std::mutex mutex;
        static bool initialized = false;
        
        std::lock_guard<std::mutex> lock(mutex);
        if (!initialized) {
            curl_global_init(CURL_GLOBAL_ALL);
            initialized = true;
            std::cout << "CURL globally initialized" << std::endl;
        }
    }
    
    static void Cleanup() {
        static std::mutex mutex;
        static bool cleaned = false;
        
        std::lock_guard<std::mutex> lock(mutex);
        if (!cleaned) {
            curl_global_cleanup();
            cleaned = true;
            std::cout << "CURL globally cleaned up" << std::endl;
        }
    }
};

/**
 * @brief 遥测工具类，用于初始化和管理OpenTelemetry相关功能
 */
class Telemetry {
public:
    /**
     * @brief 初始化遥测系统
     * @param service_name 服务名称
     * @param service_version 服务版本
     * @param endpoint 导出器端点地址
     */
    static void InitTelemetry(const std::string& service_name,
                             const std::string& service_version,
                             const std::string& endpoint = GetDefaultZipkinEndpoint()) {
        
        // 创建资源属性
        resource::ResourceAttributes attributes = {
            {service::kServiceName, service_name},
            {service::kServiceVersion, service_version},
            {host::kHostName, GetHostName()}
        };
        auto resource = opentelemetry::sdk::resource::Resource::Create(attributes);

        // 初始化Zipkin导出器
        std::unique_ptr<trace_sdk::SpanExporter> exporter;
        
        // 首先初始化CURL，避免多线程竞争
        CurlInitializer::Initialize();
        
        // 延迟初始化避免CURL竞争
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        zipkin::ZipkinExporterOptions opts;
        opts.endpoint = endpoint;
        opts.service_name = service_name;
        
        // 创建Zipkin导出器
        exporter = zipkin::ZipkinExporterFactory::Create(opts);
        
        std::cout << "Zipkin exporter initialized successfully for " << service_name 
                 << " -> " << endpoint << std::endl;
        
        // 创建处理器
        auto processor = trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter));
        
        // 创建TracerProvider
        std::shared_ptr<opentelemetry::trace::TracerProvider> provider =
                trace_sdk::TracerProviderFactory::Create(std::move(processor), std::move(resource));
        
        // 设置Trace provider
        trace::Provider::SetTracerProvider(provider);
        
        // 设置全局传播器
        auto propagator = nostd::shared_ptr<propagation::TextMapPropagator>(
            new opentelemetry::trace::propagation::HttpTraceContext());
        propagation::GlobalTextMapPropagator::SetGlobalPropagator(propagator);
    }
    
    /**
     * @brief 获取Tracer实例
     * @param library_name 库名称
     * @param library_version 库版本
     * @return Tracer实例
     */
    static nostd::shared_ptr<trace::Tracer> GetTracer(
        const std::string& library_name = "chat-service",
        const std::string& library_version = "1.0.0") {
        
        auto provider = trace::Provider::GetTracerProvider();
        return provider->GetTracer(library_name, library_version);
    }
    
    /**
     * @brief 清理遥测系统
     */
    static void CleanupTelemetry() {
        std::shared_ptr<trace::TracerProvider> none;
        trace::Provider::SetTracerProvider(none);
        
        // 清理CURL
        CurlInitializer::Cleanup();
    }

private:
    /**
     * @brief 获取默认Zipkin端点
     * @return Zipkin端点地址
     */
    static std::string GetDefaultZipkinEndpoint() {
        // 使用编译时定义的宏，如果没有定义则使用默认值
        #ifndef ZIPKIN_HOST
        #define ZIPKIN_HOST "192.168.159.138"
        #endif
        
        #ifndef ZIPKIN_PORT
        #define ZIPKIN_PORT "9411"
        #endif
        
        return std::string("http://") + ZIPKIN_HOST + ":" + ZIPKIN_PORT + "/api/v2/spans";
    }

    /**
     * @brief 获取主机名
     * @return 主机名
     */
    static std::string GetHostName() {
        char hostname[1024];
        hostname[1023] = '\0';
        if (gethostname(hostname, 1023) == -1) {
            return "unknown-host";
        }
        return std::string(hostname);
    }
};

/**
 * @brief 获取当前激活的span
 * @return 当前span的共享指针
 */
inline nostd::shared_ptr<trace::Span> GetCurrentSpan() {
    return trace::GetSpan(context::RuntimeContext::GetCurrent());
}

/**
 * @brief 创建一个有范围的跟踪span
 * 
 * 使用方法：
 * auto span = Telemetry::GetTracer()->StartSpan("span_name");
 * auto scope = trace::Scope(span);
 * 
 * @param name span名称
 * @return 返回一个Scope对象，离开作用域时会自动结束span
 */
inline trace::Scope CreateSpan(const std::string& name) {
    auto tracer = Telemetry::GetTracer();
    auto span = tracer->StartSpan(name);
    return trace::Scope(span);
}

/**
 * @brief 为gRPC调用创建子span，接受父span作为参数
 * @param parent_span 父span
 * @param name 子span名称
 * @return 返回span，调用者需要手动创建scope并调用span->End()
 */
inline nostd::shared_ptr<trace::Span> 
CreateChildSpan(const nostd::shared_ptr<trace::Span>& parent_span, const std::string& name) {
    auto tracer = Telemetry::GetTracer();
    trace::StartSpanOptions options;
    options.parent = parent_span->GetContext();
    
    auto span = tracer->StartSpan(name, {}, options);
    
    return span;
}

/**
 * @brief 带有自动管理生命周期的Span类
 */
class ScopedSpan {
public:
    ScopedSpan(const std::string& span_name) 
        : span_(CreateSpan(span_name)), scope_(span_) {
    }
    
    void AddEvent(const std::string& name) {
        span_->AddEvent(name);
    }
    
    void AddEvent(const std::string& name, const std::map<std::string, std::string>& attributes) {
        // 先添加事件
        span_->AddEvent(name);
        
        // 然后通过 SetAttribute 添加属性
        for (const auto& attr : attributes) {
            span_->SetAttribute(attr.first, attr.second);
        }
    }
    
    void SetAttribute(const std::string& key, const std::string& value) {
        span_->SetAttribute(key, value);
    }
    
    void SetStatus(trace::StatusCode code, const std::string& description = "") {
        span_->SetStatus(code, description);
    }
    
    void RecordException(const std::exception& exception) {
        span_->AddEvent("exception", {{"exception.type", typeid(exception).name()},
                                     {"exception.message", exception.what()}});
        span_->SetStatus(trace::StatusCode::kError, exception.what());
    }
    
    ~ScopedSpan() {
        span_->End();
    }

private:
    // 创建span的辅助方法
    static nostd::shared_ptr<trace::Span> CreateSpan(const std::string& span_name) {
        auto tracer = Telemetry::GetTracer();
        
        // 获取当前span上下文作为父上下文
        auto current_ctx = context::RuntimeContext::GetCurrent();
        auto parent_span = trace::GetSpan(current_ctx);
        
        trace::StartSpanOptions options;
        if (parent_span && parent_span->GetContext().IsValid()) {
            options.parent = parent_span->GetContext();
        }
        
        return tracer->StartSpan(span_name, {}, options);
    }

    nostd::shared_ptr<trace::Span> span_;
    trace::Scope scope_;
};

#endif // CHAT_SERVICE_TELEMETRY_H 