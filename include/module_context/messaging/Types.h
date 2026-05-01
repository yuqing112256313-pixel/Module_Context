#pragma once

#include "module_context/framework/Export.h"

#include "foundation/config/ConfigValue.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace module_context {
namespace messaging {

enum class ExchangeType {
    Direct = 0,
    Fanout = 1,
    Topic = 2,
    Headers = 3
};

enum class ConsumeAction {
    Ack = 0,
    Requeue = 1,
    Reject = 2
};

enum class ConnectionState {
    Created = 0,
    Connecting = 1,
    Connected = 2,
    Reconnecting = 3,
    Stopped = 4,
    Error = 5
};

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

struct MC_FRAMEWORK_API BindingSpec {
    std::string exchange;
    std::string queue;
    std::string routing_key;
    foundation::config::ConfigValue::Object arguments;
};

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

typedef std::function<ConsumeAction(const IncomingMessage&)> MessageHandler;

}  // namespace messaging
}  // namespace module_context
