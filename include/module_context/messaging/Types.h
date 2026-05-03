#pragma once

#include "module_context/framework/Export.h"

#include "foundation/config/ConfigValue.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace module_context {
namespace messaging {

/**
 * @brief AMQP 交换机类型。
 */
enum class ExchangeType {
    Direct = 0,
    Fanout = 1,
    Topic = 2,
    Headers = 3
};

/**
 * @brief 消费回调对一条消息的处置结果。
 *
 * `Ack` 表示处理成功；`Requeue` 表示拒绝并要求 broker 重新投递；
 * `Reject` 表示拒绝且不要求重新入队。开启 `ConsumerSpec::auto_ack` 时，
 * 该返回值不会再触发显式确认。
 */
enum class ConsumeAction {
    Ack = 0,
    Requeue = 1,
    Reject = 2
};

/**
 * @brief 消息总线连接状态。
 *
 * 状态描述模块内部 AMQP 连接驱动的运行阶段，不代表 broker 集群健康度。
 */
enum class ConnectionState {
    Created = 0,
    Connecting = 1,
    Connected = 2,
    Reconnecting = 3,
    Stopped = 4,
    Error = 5
};

/**
 * @brief AMQP 连接状态变化事件。
 *
 * 事件只描述模块内部连接驱动的状态转移；`reason` 用于记录触发变化的连接错误、
 * 停机原因或启动阶段说明，不应被业务代码当作稳定协议字段解析。
 */
struct MC_FRAMEWORK_API ConnectionStateChange {
    ConnectionState previous_state;
    ConnectionState current_state;
    std::string reason;

    ConnectionStateChange()
        : previous_state(ConnectionState::Created),
          current_state(ConnectionState::Created),
          reason() {
    }
};

/**
 * @brief 消息总线能力开关。
 *
 * 能力描述当前服务实现是否支持某类 AMQP 行为。它不是业务配置项，也不表示当前
 * 连接一定已经就绪。
 */
enum class MessageBusFeature {
    /**
     * @brief 支持等待 broker publisher confirm。
     */
    PublisherConfirm = 0,
    /**
     * @brief 支持 AMQP mandatory 返回不可路由消息。
     */
    MandatoryReturn = 1
};

/**
 * @brief 可靠发布完成后的 broker 处置结果。
 */
enum class PublishDisposition {
    /**
     * @brief broker 已确认接收发布命令。
     */
    BrokerAccepted = 0,
    /**
     * @brief broker 对发布命令返回 negative acknowledgement。
     */
    BrokerRejected = 1,
    /**
     * @brief 消息带 mandatory 语义发布，但 broker 未能路由到队列。
     */
    Returned = 2
};

/**
 * @brief 消息总线平台错误分类。
 *
 * 该分类补充 `foundation::base::Result` 的通用错误码，用于让调用方区分 AMQP
 * 连接、发布确认、mandatory return 等协议层语义。
 */
enum class MessageBusError {
    None = 0,
    InvalidArgument = 1,
    InvalidState = 2,
    NotConnected = 3,
    ConnectionFailed = 4,
    ConnectionClosed = 5,
    PublishTimedOut = 6,
    BrokerRejected = 7,
    MessageReturned = 8,
    UnsupportedFeature = 9,
    OperationFailed = 10,
    ProtocolError = 11,
    TransportError = 12
};

/**
 * @brief 消息总线错误诊断信息。
 */
struct MC_FRAMEWORK_API MessageBusErrorInfo {
    MessageBusError error;
    ConnectionState connection_state;
    int reply_code;
    std::string reply_text;
    std::string message;

    MessageBusErrorInfo()
        : error(MessageBusError::None),
          connection_state(ConnectionState::Created),
          reply_code(0),
          reply_text(),
          message() {
    }
};

/**
 * @brief 交换机声明参数。
 */
struct MC_FRAMEWORK_API ExchangeSpec {
    std::string name;
    ExchangeType type;
    bool durable;
    bool auto_delete;
    bool passive;
    bool internal;
    foundation::config::ConfigValue::Object arguments;

    ExchangeSpec()
        : name(),
          type(ExchangeType::Direct),
          durable(false),
          auto_delete(false),
          passive(false),
          internal(false),
          arguments() {
    }
};

/**
 * @brief 队列声明参数。
 */
struct MC_FRAMEWORK_API QueueSpec {
    std::string name;
    bool durable;
    bool auto_delete;
    bool passive;
    bool exclusive;
    foundation::config::ConfigValue::Object arguments;

    QueueSpec()
        : name(),
          durable(false),
          auto_delete(false),
          passive(false),
          exclusive(false),
          arguments() {
    }
};

/**
 * @brief 队列绑定参数。
 */
struct MC_FRAMEWORK_API BindingSpec {
    std::string exchange;
    std::string queue;
    std::string routing_key;
    foundation::config::ConfigValue::Object arguments;
};

/**
 * @brief 消费者启动参数。
 */
struct MC_FRAMEWORK_API ConsumerSpec {
    std::string name;
    std::string queue;
    std::string consumer_tag;
    bool auto_start;
    bool auto_ack;
    bool exclusive;
    bool no_local;
    std::uint16_t prefetch_count;
    foundation::config::ConfigValue::Object arguments;

    ConsumerSpec()
        : name(),
          queue(),
          consumer_tag(),
          auto_start(true),
          auto_ack(false),
          exclusive(false),
          no_local(false),
          prefetch_count(1),
          arguments() {
    }
};

/**
 * @brief AMQP 消息基础属性。
 *
 * 该结构对应 AMQP 0-9-1 basic properties 中常用且跨 broker 通用的字段。
 * 字符串为空表示未设置该属性；`priority` 与 `timestamp` 需要通过对应
 * `has_*` 字段区分“未设置”和数值 0。
 */
struct MC_FRAMEWORK_API MessageProperties {
    std::string content_type;
    std::string content_encoding;
    std::string correlation_id;
    std::string reply_to;
    std::string expiration;
    std::string message_id;
    std::string type;
    std::string user_id;
    std::string app_id;
    foundation::config::ConfigValue::Object headers;
    bool persistent;
    bool has_priority;
    std::uint8_t priority;
    bool has_timestamp;
    std::uint64_t timestamp;

    MessageProperties()
        : content_type(),
          content_encoding(),
          correlation_id(),
          reply_to(),
          expiration(),
          message_id(),
          type(),
          user_id(),
          app_id(),
          headers(),
          persistent(false),
          has_priority(false),
          priority(0),
          has_timestamp(false),
          timestamp(0) {
    }
};

/**
 * @brief 发布请求。
 *
 * `exchange` 为空时表示 AMQP 默认交换机，此时 `routing_key` 应为目标队列名。
 * 对 fanout 等不使用 routing key 的交换机，`routing_key` 可以为空。
 */
struct MC_FRAMEWORK_API PublishRequest {
    std::string exchange;
    std::string routing_key;
    std::vector<char> payload;
    MessageProperties properties;
    bool mandatory;

    PublishRequest()
        : exchange(),
          routing_key(),
          payload(),
          properties(),
          mandatory(false) {
    }
};

/**
 * @brief broker 确认发布的等待选项。
 */
struct MC_FRAMEWORK_API PublishConfirmOptions {
    /**
     * @brief 最长等待确认时间，单位毫秒。
     *
     * 小于等于 0 表示无限等待。平台代码通常应给出明确超时，避免调用线程被网络
     * 或 broker 状态长期挂住。
     */
    int timeout_ms;
    /**
     * @brief 是否要求消息至少路由到一个队列。
     *
     * 开启后服务会使用 AMQP mandatory 语义发布本次消息。若 broker 返回
     * basic.return，`PublishConfirmed()` 会返回 `Returned` 处置结果。
     */
    bool require_routable;

    PublishConfirmOptions()
        : timeout_ms(5000),
          require_routable(false) {
    }
};

/**
 * @brief broker 确认发布后的回执。
 */
struct MC_FRAMEWORK_API PublishReceipt {
    PublishDisposition disposition;
    int reply_code;
    std::string reply_text;
    MessageBusErrorInfo error;

    PublishReceipt()
        : disposition(PublishDisposition::BrokerAccepted),
          reply_code(0),
          reply_text(),
          error() {
    }
};

/**
 * @brief 传递给消费者回调的入站消息。
 */
struct MC_FRAMEWORK_API IncomingMessage {
    std::string consumer_name;
    std::string exchange;
    std::string routing_key;
    std::vector<char> payload;
    MessageProperties properties;
    bool redelivered;

    IncomingMessage()
        : consumer_name(),
          exchange(),
          routing_key(),
          payload(),
          properties(),
          redelivered(false) {
    }
};

/**
 * @brief 消息处理回调。
 */
typedef std::function<ConsumeAction(const IncomingMessage&)> MessageHandler;

/**
 * @brief 连接状态变化回调。
 */
typedef std::function<void(const ConnectionStateChange&)> ConnectionStateHandler;

}  // namespace messaging
}  // namespace module_context
