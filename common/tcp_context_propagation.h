#ifndef TCP_CONTEXT_PROPAGATION_H
#define TCP_CONTEXT_PROPAGATION_H

#include <cstring>
#include <vector>
#include <sstream>
#include <iomanip>
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/context/runtime_context.h"
#include "telemetry.h"

namespace tcp_context_propagation {

using namespace opentelemetry;

/**
 * @brief 简化的TCP追踪上下文数据结构
 * 只包含W3C Trace Context的核心字段，支持协议扩展
 * 总大小：31字节（固定长度，网络传输高效）
 */
struct TcpTraceContext {
    static constexpr uint32_t MAGIC_NUMBER = 0x4F544C59;  // 'OTLY' - OpenTelemetry
    static constexpr uint16_t VERSION = 0x0001;           // 协议版本
    
    // 协议头部 (6字节)
    uint32_t magic;          // 魔数，用于验证数据完整性和协议识别
    uint16_t version;        // 协议版本，支持向后兼容和渐进式升级
    
    // W3C Trace Context 核心字段 (25字节)
    uint8_t trace_id[16];    // 128位追踪ID
    uint8_t span_id[8];      // 64位SpanID  
    uint8_t trace_flags;     // 追踪标志 (sampled, etc.)
    
    TcpTraceContext() {
        magic = MAGIC_NUMBER;
        version = VERSION;
        std::memset(trace_id, 0, sizeof(trace_id));
        std::memset(span_id, 0, sizeof(span_id));
        trace_flags = 0;
    }
    
    /**
     * @brief 验证数据结构的有效性
     */
    bool IsValid() const {
        return magic == MAGIC_NUMBER && version <= VERSION && HasValidTraceData();
    }
    
    /**
     * @brief 检查是否包含有效的追踪数据
     */
    bool HasValidTraceData() const {
        // 检查trace_id是否非零
        for (int i = 0; i < 16; ++i) {
            if (trace_id[i] != 0) return true;
        }
        return false;
    }
    
    /**
     * @brief 获取固定大小（网络传输时使用）
     */
    static constexpr size_t GetSize() {
        return 4 + 2 + 16 + 8 + 1; // 31字节
    }
};



/**
 * @brief 简化的TCP追踪上下文传播器
 * 直接操作span context，无需复杂的载体和baggage处理
 */
class TcpTracePropagator {
public:
    /**
     * @brief 从当前span获取追踪信息并序列化为二进制数据
     * @return 序列化后的二进制数据（固定31字节）
     */
    static std::vector<uint8_t> SerializeCurrentContext() {
        TcpTraceContext ctx;
        
        // 获取当前span的上下文
        auto current_span = GetCurrentSpan();
        if (current_span && current_span->GetContext().IsValid()) {
            auto span_context = current_span->GetContext();
            
            // 复制TraceId (16字节)
            span_context.trace_id().CopyBytesTo(
                nostd::span<uint8_t, 16>(ctx.trace_id, 16)
            );
            
            // 复制SpanId (8字节)
            span_context.span_id().CopyBytesTo(
                nostd::span<uint8_t, 8>(ctx.span_id, 8)
            );
            
            // 复制TraceFlags (1字节)
            ctx.trace_flags = span_context.trace_flags().flags();
        }
        
        return SerializeContext(ctx);
    }
    
    /**
     * @brief 将追踪上下文序列化为二进制数据
     * @param ctx 追踪上下文
     * @return 序列化后的二进制数据（固定31字节）
     */
    static std::vector<uint8_t> SerializeContext(const TcpTraceContext& ctx) {
        std::vector<uint8_t> result;
        result.reserve(TcpTraceContext::GetSize());
        
        // 序列化固定字段（网络字节序）
        SerializeField(result, htonl(ctx.magic));
        SerializeField(result, htons(ctx.version));
        
        // 追踪ID和SpanID（保持原始字节序）
        result.insert(result.end(), ctx.trace_id, ctx.trace_id + 16);
        result.insert(result.end(), ctx.span_id, ctx.span_id + 8);
        
        // 追踪标志
        result.push_back(ctx.trace_flags);
        
        return result;
    }
    
    /**
     * @brief 从二进制数据反序列化追踪上下文
     * @param data 二进制数据
     * @return 反序列化的追踪上下文，如果失败返回无效上下文
     */
    static TcpTraceContext DeserializeContext(const std::vector<uint8_t>& data) {
        return DeserializeContext(data.data(), data.size());
    }
    
    /**
     * @brief 从二进制数据反序列化追踪上下文
     * @param data 二进制数据指针
     * @param size 数据大小
     * @return 反序列化的追踪上下文，如果失败返回无效上下文
     */
    static TcpTraceContext DeserializeContext(const uint8_t* data, size_t size) {
        TcpTraceContext ctx;
        
        if (size < TcpTraceContext::GetSize()) {
            return ctx; // 数据不完整，返回无效上下文
        }
        
        size_t offset = 0;
        
        // 反序列化固定字段
        ctx.magic = ntohl(DeserializeField<uint32_t>(data, offset));
        ctx.version = ntohs(DeserializeField<uint16_t>(data, offset));
        
        if (!ctx.IsValid()) {
            return TcpTraceContext(); // 返回无效的上下文
        }
        
        // 反序列化追踪ID和SpanID
        std::memcpy(ctx.trace_id, data + offset, 16);
        offset += 16;
        std::memcpy(ctx.span_id, data + offset, 8);
        offset += 8;
        
        // 反序列化追踪标志
        ctx.trace_flags = data[offset];
        
        return ctx;
    }
    
    /**
     * @brief 应用追踪上下文到当前线程
     * @param ctx 追踪上下文
     * @return 上下文令牌，用于恢复原始上下文
     */
    static nostd::unique_ptr<context::Token> ApplyContext(const TcpTraceContext& ctx) {
        if (!ctx.HasValidTraceData()) {
            return nullptr; // 无效数据，不做任何处理
        }
        
        // 创建TraceId和SpanId
        auto trace_id = trace::TraceId(ctx.trace_id);
        auto span_id = trace::SpanId(ctx.span_id);
        auto trace_flags = trace::TraceFlags(ctx.trace_flags);
        
        // 创建远程span context
        auto remote_span_context = trace::SpanContext(
            trace_id, 
            span_id,
            trace_flags,
            true  // is_remote
        );
        
        // 创建远程span并设置为当前上下文
        auto tracer = Telemetry::GetTracer();
        auto remote_span = tracer->StartSpan("remote_operation", 
            trace::StartSpanOptions{.parent = remote_span_context});
        
        // 将span设置到当前上下文
        auto current_ctx = context::RuntimeContext::GetCurrent();
        auto new_context = trace::SetSpan(current_ctx, remote_span);
        
        return context::RuntimeContext::Attach(new_context);
    }
    
    /**
     * @brief 从二进制数据直接应用追踪上下文
     * @param data 二进制数据
     * @return 上下文令牌，用于恢复原始上下文
     */
    static nostd::unique_ptr<context::Token> ApplyContextFromBinary(const std::vector<uint8_t>& data) {
        auto ctx = DeserializeContext(data);
        return ApplyContext(ctx);
    }
    
    /**
     * @brief 从二进制数据直接应用追踪上下文
     * @param data 二进制数据指针
     * @param size 数据大小
     * @return 上下文令牌，用于恢复原始上下文
     */
    static nostd::unique_ptr<context::Token> ApplyContextFromBinary(const uint8_t* data, size_t size) {
        auto ctx = DeserializeContext(data, size);
        return ApplyContext(ctx);
    }

private:
    /**
     * @brief 序列化字段到字节数组
     */
    template<typename T>
    static void SerializeField(std::vector<uint8_t>& buffer, T value) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&value);
        buffer.insert(buffer.end(), ptr, ptr + sizeof(T));
    }
    
    /**
     * @brief 从字节数组反序列化字段
     */
    template<typename T>
    static T DeserializeField(const uint8_t* data, size_t& offset) {
        T value;
        std::memcpy(&value, data + offset, sizeof(T));
        offset += sizeof(T);
        return value;
    }
};

/**
 * @brief 便捷函数：获取当前追踪上下文的二进制数据
 * 在发送TCP消息前调用，返回固定31字节的数据
 */
inline std::vector<uint8_t> GetCurrentTraceContextBinary() {
    return TcpTracePropagator::SerializeCurrentContext();
}

/**
 * @brief 便捷函数：从TCP消息中的二进制数据恢复追踪上下文
 * 在接收TCP消息后调用
 */
inline nostd::unique_ptr<context::Token> SetTraceContextFromBinary(const std::vector<uint8_t>& data) {
    return TcpTracePropagator::ApplyContextFromBinary(data);
}

/**
 * @brief 便捷函数：从TCP消息中的二进制数据恢复追踪上下文
 * 在接收TCP消息后调用
 */
inline nostd::unique_ptr<context::Token> SetTraceContextFromBinary(const uint8_t* data, size_t size) {
    return TcpTracePropagator::ApplyContextFromBinary(data, size);
}

} // namespace tcp_context_propagation

#endif // TCP_CONTEXT_PROPAGATION_H