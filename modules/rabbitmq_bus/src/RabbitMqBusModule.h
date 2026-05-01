#pragma once

#include "module_context/messaging/IMessageBusService.h"
#include "framework/ModuleBase.h"

#include "foundation/base/Result.h"

#include <memory>
#include <string>

namespace module_context {
namespace messaging {

struct RabbitMqBusSharedState;
class MessageBusServiceProxy;

class RabbitMqBusModule final
    : public module_context::framework::ModuleBase,
      public IMessageBusService {
public:
    RabbitMqBusModule();
    ~RabbitMqBusModule() override;

    std::string ModuleType() const override;
    std::string ModuleVersion() const override;

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

protected:
    foundation::base::Result<void> OnInit() override;
    foundation::base::Result<void> OnStart() override;
    foundation::base::Result<void> OnStop() override;
    foundation::base::Result<void> OnFini() override;

private:
    std::shared_ptr<RabbitMqBusSharedState> shared_state_;
    std::unique_ptr<MessageBusServiceProxy> service_proxy_;
};

}  // namespace messaging
}  // namespace module_context
