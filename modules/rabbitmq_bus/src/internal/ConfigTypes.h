#pragma once

#include "module_context/messaging/Types.h"

#include "foundation/config/ConfigValue.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace module_context {
namespace messaging {

/**
 * @brief 连接断开后的重连策略。
 */
struct ReconnectConfig {
    bool enabled;
    int initial_delay_ms;
    int max_delay_ms;

    ReconnectConfig()
        : enabled(true),
          initial_delay_ms(1000),
          max_delay_ms(30000) {
    }
};

/**
 * @brief AMQP 连接参数。
 */
struct ConnectionConfig {
    std::string uri;
    std::uint16_t heartbeat_seconds;
    int connect_timeout_ms;
    int socket_timeout_ms;
    ReconnectConfig reconnect;

    ConnectionConfig()
        : uri(),
          heartbeat_seconds(30),
          connect_timeout_ms(5000),
          socket_timeout_ms(100) {
    }
};

/**
 * @brief 预定义发布端配置。
 *
 * 当前公开服务以 `PublishRequest` 直接发布消息；该结构保留配置中的发布端元信息，
 * 便于后续增加具名 publisher 或统一默认 headers。
 */
struct PublisherSpec {
    std::string name;
    std::string exchange;
    std::string routing_key;
    std::string content_type;
    foundation::config::ConfigValue::Object headers;
    bool persistent;

    PublisherSpec()
        : name(),
          exchange(),
          routing_key(),
          content_type(),
          headers(),
          persistent(false) {
    }
};

/**
 * @brief rabbitmq_bus 模块完整运行配置。
 *
 * 字段采用 AMQP 通用语义。模块名仍保留 `rabbitmq_bus` 是为了兼容现有配置和
 * 构建产物；实现主体没有依赖 RabbitMQ Management API。
 */
struct RabbitMqBusConfig {
    ConnectionConfig connection;
    std::size_t worker_thread_count;
    std::vector<ExchangeSpec> exchanges;
    std::vector<QueueSpec> queues;
    std::vector<BindingSpec> bindings;
    std::vector<PublisherSpec> publishers;
    std::vector<ConsumerSpec> consumers;

    RabbitMqBusConfig()
        : connection(),
          worker_thread_count(4),
          exchanges(),
          queues(),
          bindings(),
          publishers(),
          consumers() {
    }
};

}  // namespace messaging
}  // namespace module_context
