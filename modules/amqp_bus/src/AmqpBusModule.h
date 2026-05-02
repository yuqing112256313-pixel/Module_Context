#pragma once

#include "module_context/messaging/IMessageBusService.h"
#include "framework/ModuleBase.h"

#include "foundation/base/Result.h"

#include <memory>
#include <string>
#include <vector>

namespace module_context {
namespace messaging {

struct AmqpBusSharedState;
class MessageBusServiceProxy;

/**
 * @brief AMQP 消息总线模块的框架外壳。
 *
 * 模块本体负责框架生命周期和配置装载，`IMessageBusService` 的实际调用路径
 * 委托给 `MessageBusServiceProxy`。这样服务能力可以通过共享状态访问当前驱动，
 * 而不把连接线程、worker pool 和模块状态机直接暴露给调用方。
 */
class AmqpBusModule final
    : public module_context::framework::ModuleBase,
      public IMessageBusService {
public:
    AmqpBusModule();
    ~AmqpBusModule() override;

    std::string ModuleType() const override;
    std::string ModuleVersion() const override;

    foundation::base::Result<void> Publish(
        const PublishRequest& request) override;
    foundation::base::Result<void> PublishAsync(
        const PublishRequest& request) override;
    foundation::base::Result<PublishReceipt> PublishConfirmed(
        const PublishRequest& request,
        const PublishConfirmOptions& options) override;
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
    bool SupportsFeature(MessageBusFeature feature) const override;

protected:
    foundation::base::Result<void> OnInit() override;
    foundation::base::Result<void> OnStart() override;
    foundation::base::Result<void> OnStop() override;
    foundation::base::Result<void> OnFini() override;

private:
    // 模块、proxy、连接驱动和 worker 回调之间共享的运行态状态。
    std::shared_ptr<AmqpBusSharedState> shared_state_;
    // 服务方法的稳定转发层；模块生命周期重建 driver 时，proxy 指针保持不变。
    std::unique_ptr<MessageBusServiceProxy> service_proxy_;
};

}  // namespace messaging
}  // namespace module_context
