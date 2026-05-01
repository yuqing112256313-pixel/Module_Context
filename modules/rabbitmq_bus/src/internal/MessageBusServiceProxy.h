#pragma once

#include "module_context/messaging/IMessageBusService.h"

#include <memory>
#include <string>

namespace module_context {
namespace messaging {

struct RabbitMqBusSharedState;

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
    std::shared_ptr<RabbitMqBusSharedState> state_;
};

}  // namespace messaging
}  // namespace module_context
