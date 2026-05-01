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

std::shared_ptr<RabbitMqConnectionDriver> LookupDriver(
    const std::shared_ptr<RabbitMqBusSharedState>& state) {
    if (!state) {
        return std::shared_ptr<RabbitMqConnectionDriver>();
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    return state->driver;
}

}  // namespace

MessageBusServiceProxy::MessageBusServiceProxy(
    const std::shared_ptr<RabbitMqBusSharedState>& state)
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

foundation::base::Result<void> MessageBusServiceProxy::RegisterConsumerHandler(
    const std::string& consumer_name,
    MessageHandler handler) {
    if (!state_ || !state_->config) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidState,
            "RabbitMQ bus shared state is unavailable");
    }

    if (consumer_name.empty()) {
        return MakeInvalidArgument("consumer_name must not be empty");
    }
    if (!handler) {
        return MakeInvalidArgument("MessageHandler must be valid");
    }

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
            "RabbitMQ bus shared state is unavailable");
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

}  // namespace messaging
}  // namespace module_context
