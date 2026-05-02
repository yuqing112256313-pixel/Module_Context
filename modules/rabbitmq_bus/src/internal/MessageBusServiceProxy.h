#pragma once

#include "module_context/messaging/IMessageBusService.h"

#include <memory>
#include <string>

namespace module_context {
namespace messaging {

struct RabbitMqBusSharedState;

/**
 * @brief `IMessageBusService` 的运行态代理。
 *
 * proxy 的存在是为了把“能力接口对象”与“模块对象/连接驱动对象”分开：
 * 调用方拿到的服务能力稳定不变，底层 driver 可以随 Start/Stop 或重连重建。
 */
class MessageBusServiceProxy : public IMessageBusService {
public:
    explicit MessageBusServiceProxy(
        const std::shared_ptr<RabbitMqBusSharedState>& state);
    ~MessageBusServiceProxy() override;

    foundation::base::Result<void> Publish(
        const PublishRequest& request) override;
    foundation::base::Result<void> PublishAsync(
        const PublishRequest& request) override;
    foundation::base::Result<void> RegisterConsumerHandler(
        const std::string& consumer_name,
        MessageHandler handler) override;
    foundation::base::Result<void> UnregisterConsumerHandler(
        const std::string& consumer_name) override;
    foundation::base::Result<void> DeclareExchange(
        const ExchangeSpec& spec) override;
    foundation::base::Result<void> DeclareQueue(
        const QueueSpec& spec) override;
    foundation::base::Result<void> BindQueue(
        const BindingSpec& spec) override;
    ConnectionState GetConnectionState() const override;

private:
    // 不拥有具体 driver，只持有共享状态以便每次调用查找当前 driver。
    std::shared_ptr<RabbitMqBusSharedState> state_;
};

}  // namespace messaging
}  // namespace module_context
