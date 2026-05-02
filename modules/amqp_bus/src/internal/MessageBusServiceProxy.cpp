#include "MessageBusServiceProxy.h"

#include "DriverOps.h"
#include "SharedState.h"

#include "foundation/base/ErrorCode.h"

#include <mutex>

namespace module_context {
namespace messaging {

namespace {

foundation::base::Result<void> MakeInvalidArgument(const std::string& message) {
    return foundation::base::Result<void>(
        foundation::base::ErrorCode::kInvalidArgument,
        message);
}

std::shared_ptr<AmqpConnectionDriver> LookupDriver(
    const std::shared_ptr<AmqpBusSharedState>& state) {
    if (!state) {
        return std::shared_ptr<AmqpConnectionDriver>();
    }

    // 每次调用都取当前 driver，而不是在 proxy 中缓存 driver。这样模块重启或重连
    // 时可以替换底层驱动，已经暴露出去的 IMessageBusService 指针仍然有效。
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->driver;
}

}  // namespace

MessageBusServiceProxy::MessageBusServiceProxy(
    const std::shared_ptr<AmqpBusSharedState>& state)
    : state_(state) {
}

MessageBusServiceProxy::~MessageBusServiceProxy() {
}

foundation::base::Result<void> MessageBusServiceProxy::Publish(
    const PublishRequest& request) {
    return PublishWithDriver(LookupDriver(state_), request);
}

foundation::base::Result<void> MessageBusServiceProxy::PublishAsync(
    const PublishRequest& request) {
    return PublishAsyncWithDriver(LookupDriver(state_), request);
}

foundation::base::Result<PublishReceipt> MessageBusServiceProxy::PublishConfirmed(
    const PublishRequest& request,
    const PublishConfirmOptions& options) {
    return PublishConfirmedWithDriver(LookupDriver(state_), request, options);
}

foundation::base::Result<void> MessageBusServiceProxy::RegisterConsumerHandler(
    const std::string& consumer_name,
    MessageHandler handler) {
    if (!state_ || !state_->config) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidState,
            "AMQP bus shared state is unavailable");
    }

    if (consumer_name.empty()) {
        return MakeInvalidArgument("consumer_name must not be empty");
    }
    if (!handler) {
        return MakeInvalidArgument("MessageHandler must be valid");
    }

    // consumer_name 是配置层声明的逻辑名，不是队列名。这里先校验配置中确实存在
    // 该 consumer，避免调用方把 queue 名误当作 handler 注册名。
    std::lock_guard<std::mutex> lock(state_->mutex);
    bool found = false;
    for (std::size_t index = 0; index < state_->config->consumers.size(); ++index) {
        if (state_->config->consumers[index].name == consumer_name) {
            found = true;
            break;
        }
    }
    if (!found) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kNotFound,
            "Unknown consumer '" + consumer_name + "'");
    }

    state_->handlers[consumer_name] = handler;
    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> MessageBusServiceProxy::UnregisterConsumerHandler(
    const std::string& consumer_name) {
    if (!state_) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidState,
            "AMQP bus shared state is unavailable");
    }

    std::lock_guard<std::mutex> lock(state_->mutex);
    std::map<std::string, MessageHandler>::iterator it =
        state_->handlers.find(consumer_name);
    if (it == state_->handlers.end()) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kNotFound,
            "Consumer handler '" + consumer_name + "' is not registered");
    }

    state_->handlers.erase(it);
    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> MessageBusServiceProxy::DeclareExchange(
    const ExchangeSpec& spec) {
    return DeclareExchangeWithDriver(LookupDriver(state_), spec);
}

foundation::base::Result<void> MessageBusServiceProxy::DeclareQueue(
    const QueueSpec& spec) {
    return DeclareQueueWithDriver(LookupDriver(state_), spec);
}

foundation::base::Result<void> MessageBusServiceProxy::BindQueue(
    const BindingSpec& spec) {
    return BindQueueWithDriver(LookupDriver(state_), spec);
}

ConnectionState MessageBusServiceProxy::GetConnectionState() const {
    if (!state_) {
        return ConnectionState::Created;
    }

    return GetConnectionStateFromDriver(LookupDriver(state_));
}

bool MessageBusServiceProxy::SupportsFeature(MessageBusFeature feature) const {
    switch (feature) {
        case MessageBusFeature::MandatoryReturn:
            return true;
        case MessageBusFeature::PublisherConfirm: {
            if (!state_) {
                return true;
            }
            std::lock_guard<std::mutex> lock(state_->mutex);
            return !state_->config || state_->config->publisher_confirms_enabled;
        }
        default:
            return false;
    }
}

}  // namespace messaging
}  // namespace module_context
