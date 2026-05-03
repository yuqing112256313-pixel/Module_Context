#pragma once

#include "AmqpBusConfig.h"

#include "module_context/messaging/Types.h"

#include "foundation/base/Result.h"
#include "foundation/config/ConfigValue.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace module_context {
namespace messaging {

class AmqpConnectionDriver;

struct DeliveryContext {
    std::string consumer_name;
    std::uint64_t delivery_tag;
    bool auto_ack;

    DeliveryContext()
        : consumer_name(),
          delivery_tag(0),
          auto_ack(false) {
    }
};

typedef std::function<void(
    const IncomingMessage&,
    const DeliveryContext&)> DeliveryCallback;

typedef std::function<void(const ConnectionStateChange&)> ConnectionStateCallback;

// DriverOps 是模块/proxy 与 cpp 内部连接驱动之间的窄桥接层。连接驱动类型不
// 暴露到公开头文件，模块外壳也不需要知道 AMQP-CPP 的具体类。
foundation::base::Result<AmqpBusConfig> ParseAmqpBusConfig(
    const foundation::config::ConfigValue& config_value);
std::shared_ptr<AmqpConnectionDriver> CreateAmqpConnectionDriver(
    const AmqpBusConfig& config,
    DeliveryCallback delivery_callback,
    ConnectionStateCallback state_callback);
foundation::base::Result<void> StartDriver(
    const std::shared_ptr<AmqpConnectionDriver>& driver);
foundation::base::Result<void> StopDriver(
    const std::shared_ptr<AmqpConnectionDriver>& driver);
foundation::base::Result<void> PublishWithDriver(
    const std::shared_ptr<AmqpConnectionDriver>& driver,
    const PublishRequest& request);
foundation::base::Result<void> PublishAsyncWithDriver(
    const std::shared_ptr<AmqpConnectionDriver>& driver,
    const PublishRequest& request);
foundation::base::Result<PublishReceipt> PublishConfirmedWithDriver(
    const std::shared_ptr<AmqpConnectionDriver>& driver,
    const PublishRequest& request,
    const PublishConfirmOptions& options);
foundation::base::Result<void> StartConsumerWithDriver(
    const std::shared_ptr<AmqpConnectionDriver>& driver,
    const std::string& consumer_name);
foundation::base::Result<void> StopConsumerWithDriver(
    const std::shared_ptr<AmqpConnectionDriver>& driver,
    const std::string& consumer_name);
foundation::base::Result<void> DeclareExchangeWithDriver(
    const std::shared_ptr<AmqpConnectionDriver>& driver,
    const ExchangeSpec& spec);
foundation::base::Result<void> DeclareQueueWithDriver(
    const std::shared_ptr<AmqpConnectionDriver>& driver,
    const QueueSpec& spec);
foundation::base::Result<void> BindQueueWithDriver(
    const std::shared_ptr<AmqpConnectionDriver>& driver,
    const BindingSpec& spec);
foundation::base::Result<void> CompleteDeliveryAsyncWithDriver(
    const std::shared_ptr<AmqpConnectionDriver>& driver,
    const DeliveryContext& delivery,
    ConsumeAction action);
ConnectionState GetConnectionStateFromDriver(
    const std::shared_ptr<AmqpConnectionDriver>& driver);
MessageBusErrorInfo GetLastErrorInfoFromDriver(
    const std::shared_ptr<AmqpConnectionDriver>& driver);
bool SupportsFeatureFromDriver(
    const std::shared_ptr<AmqpConnectionDriver>& driver,
    MessageBusFeature feature);

}  // namespace messaging
}  // namespace module_context
