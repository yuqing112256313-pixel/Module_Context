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
    bool auto_ack;
    bool exclusive;
    bool no_local;
    std::uint16_t prefetch_count;
    foundation::config::ConfigValue::Object arguments;

    ConsumerSpec()
        : name(),
          queue(),
          consumer_tag(),
          auto_ack(false),
          exclusive(false),
          no_local(false),
          prefetch_count(1),
          arguments() {
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
    std::string content_type;
    std::string correlation_id;
    std::string reply_to;
    foundation::config::ConfigValue::Object headers;
    bool persistent;
    bool mandatory;

    PublishRequest()
        : exchange(),
          routing_key(),
          payload(),
          content_type(),
          correlation_id(),
          reply_to(),
          headers(),
          persistent(false),
          mandatory(false) {
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
    std::string content_type;
    std::string correlation_id;
    std::string reply_to;
    foundation::config::ConfigValue::Object headers;
    bool redelivered;

    IncomingMessage()
        : consumer_name(),
          exchange(),
          routing_key(),
          payload(),
          content_type(),
          correlation_id(),
          reply_to(),
          headers(),
          redelivered(false) {
    }
};

/**
 * @brief 消息处理回调。
 */
typedef std::function<ConsumeAction(const IncomingMessage&)> MessageHandler;

}  // namespace messaging
}  // namespace module_context
