#pragma once

#include "module_context/messaging/Types.h"

#include "foundation/base/Result.h"

#include <memory>

namespace module_context {
namespace messaging {

class RabbitMqConnectionDriver;

// DriverOps 是 proxy 与 cpp 内部连接驱动之间的窄桥接层。连接驱动类型不暴露到
// 公开头文件，proxy 也不需要知道 AMQP-CPP 的具体类。
foundation::base::Result<void> PublishWithDriver(
    const std::shared_ptr<RabbitMqConnectionDriver>& driver,
    const PublishRequest& request);
foundation::base::Result<void> PublishAsyncWithDriver(
    const std::shared_ptr<RabbitMqConnectionDriver>& driver,
    const PublishRequest& request);
foundation::base::Result<void> DeclareExchangeWithDriver(
    const std::shared_ptr<RabbitMqConnectionDriver>& driver,
    const ExchangeSpec& spec);
foundation::base::Result<void> DeclareQueueWithDriver(
    const std::shared_ptr<RabbitMqConnectionDriver>& driver,
    const QueueSpec& spec);
foundation::base::Result<void> BindQueueWithDriver(
    const std::shared_ptr<RabbitMqConnectionDriver>& driver,
    const BindingSpec& spec);
ConnectionState GetConnectionStateFromDriver(
    const std::shared_ptr<RabbitMqConnectionDriver>& driver);

}  // namespace messaging
}  // namespace module_context
