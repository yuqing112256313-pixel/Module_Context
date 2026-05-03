#include "AmqpBusModule.h"

#include "internal/DriverOps.h"
#include "internal/MessageBusServiceProxy.h"
#include "internal/SharedState.h"

#include "module_context/framework/IModuleManager.h"
#include "module_context/plugin/ModuleFactory.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/concurrent/ThreadPool.h"
#include "foundation/log/Logger.h"

#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace module_context {
namespace messaging {

namespace {

foundation::base::Result<void> MakeInvalidState(const std::string& message) {
    return foundation::base::Result<void>(
        foundation::base::ErrorCode::kInvalidState,
        message);
}

void LogResultIfError(const foundation::base::Result<void>& result,
                      const std::string& message) {
    if (!result.IsOk()) {
        FOUNDATION_LOG_ERROR(message << ": " << result.GetMessage());
    }
}

void InvokeConnectionStateHandler(
    const std::string& handler_name,
    ConnectionStateHandler handler,
    const ConnectionStateChange& change) {
    try {
        handler(change);
    } catch (...) {
        FOUNDATION_LOG_ERROR(
            "Connection state handler '" << handler_name
            << "' threw an exception");
    }
}

void DispatchConnectionStateChange(
    const std::shared_ptr<AmqpBusSharedState>& state,
    const ConnectionStateChange& change) {
    std::vector<std::pair<std::string, ConnectionStateHandler> > handlers;
    std::shared_ptr<foundation::concurrent::ThreadPool> worker_pool;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        for (std::map<std::string, ConnectionStateHandler>::const_iterator it =
                 state->state_handlers.begin();
             it != state->state_handlers.end();
             ++it) {
            handlers.push_back(*it);
        }
        worker_pool = state->worker_pool;
    }

    for (std::size_t index = 0; index < handlers.size(); ++index) {
        const std::string handler_name = handlers[index].first;
        ConnectionStateHandler handler = handlers[index].second;
        if (worker_pool && !worker_pool->IsStopped()) {
            foundation::base::Result<std::future<void> > submitted =
                worker_pool->Submit(
                    [handler_name, handler, change]() {
                        InvokeConnectionStateHandler(
                            handler_name,
                            handler,
                            change);
                    });
            if (submitted.IsOk()) {
                continue;
            }
        }

        // 停机阶段 worker pool 可能已经关闭，此时同步兜底以保证 Stopped/Error
        // 事件不会丢失。状态观察者应保持轻量，不应在回调中长时间阻塞。
        InvokeConnectionStateHandler(handler_name, handler, change);
    }
}

}  // namespace

AmqpBusModule::AmqpBusModule()
    : shared_state_(new AmqpBusSharedState()),
      service_proxy_(new MessageBusServiceProxy(shared_state_)) {
}

AmqpBusModule::~AmqpBusModule() {
}

std::string AmqpBusModule::ModuleType() const {
    return "amqp_bus";
}

std::string AmqpBusModule::ModuleVersion() const {
    return "1.0.0";
}

foundation::base::Result<void> AmqpBusModule::Publish(
    const PublishRequest& request) {
    return service_proxy_->Publish(request);
}

foundation::base::Result<void> AmqpBusModule::PublishAsync(
    const PublishRequest& request) {
    return service_proxy_->PublishAsync(request);
}

foundation::base::Result<PublishReceipt> AmqpBusModule::PublishConfirmed(
    const PublishRequest& request,
    const PublishConfirmOptions& options) {
    return service_proxy_->PublishConfirmed(request, options);
}

foundation::base::Result<void> AmqpBusModule::RegisterConsumerHandler(
    const std::string& consumer_name,
    MessageHandler handler) {
    return service_proxy_->RegisterConsumerHandler(consumer_name, handler);
}

foundation::base::Result<void> AmqpBusModule::UnregisterConsumerHandler(
    const std::string& consumer_name) {
    return service_proxy_->UnregisterConsumerHandler(consumer_name);
}

foundation::base::Result<void> AmqpBusModule::StartConsumer(
    const std::string& consumer_name) {
    return service_proxy_->StartConsumer(consumer_name);
}

foundation::base::Result<void> AmqpBusModule::StopConsumer(
    const std::string& consumer_name) {
    return service_proxy_->StopConsumer(consumer_name);
}

foundation::base::Result<void> AmqpBusModule::RegisterConnectionStateHandler(
    const std::string& handler_name,
    ConnectionStateHandler handler) {
    return service_proxy_->RegisterConnectionStateHandler(handler_name, handler);
}

foundation::base::Result<void> AmqpBusModule::UnregisterConnectionStateHandler(
    const std::string& handler_name) {
    return service_proxy_->UnregisterConnectionStateHandler(handler_name);
}

foundation::base::Result<void> AmqpBusModule::DeclareExchange(
    const ExchangeSpec& spec) {
    return service_proxy_->DeclareExchange(spec);
}

foundation::base::Result<void> AmqpBusModule::DeclareQueue(
    const QueueSpec& spec) {
    return service_proxy_->DeclareQueue(spec);
}

foundation::base::Result<void> AmqpBusModule::BindQueue(
    const BindingSpec& spec) {
    return service_proxy_->BindQueue(spec);
}

ConnectionState AmqpBusModule::GetConnectionState() const {
    return service_proxy_->GetConnectionState();
}

bool AmqpBusModule::SupportsFeature(MessageBusFeature feature) const {
    return service_proxy_->SupportsFeature(feature);
}

foundation::base::Result<void> AmqpBusModule::OnInit() {
    module_context::framework::IModuleManager* manager = Context().ModuleManager();
    if (manager == NULL) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidState,
            "Module manager is unavailable");
    }

    foundation::base::Result<foundation::config::ConfigValue> config =
        manager->ModuleConfig(ModuleName());
    if (!config.IsOk()) {
        return foundation::base::Result<void>(
            config.GetError(),
            config.GetMessage());
    }

    foundation::base::Result<AmqpBusConfig> parsed =
        ParseAmqpBusConfig(config.Value());
    if (!parsed.IsOk()) {
        return foundation::base::Result<void>(
            parsed.GetError(),
            parsed.GetMessage());
    }

    std::shared_ptr<AmqpBusConfig> parsed_config(
        new AmqpBusConfig(parsed.Value()));
    std::shared_ptr<foundation::concurrent::ThreadPool> worker_pool(
        new foundation::concurrent::ThreadPool(parsed_config->worker_thread_count));
    {
        std::lock_guard<std::mutex> lock(shared_state_->mutex);
        shared_state_->config = parsed_config;
        shared_state_->handlers.clear();
        shared_state_->state_handlers.clear();
        shared_state_->worker_pool = worker_pool;
        shared_state_->driver.reset();
        shared_state_->stopping = false;
    }

    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> AmqpBusModule::OnStart() {
    std::shared_ptr<AmqpBusConfig> config;
    std::shared_ptr<foundation::concurrent::ThreadPool> worker_pool;
    {
        std::lock_guard<std::mutex> lock(shared_state_->mutex);
        shared_state_->stopping = false;
        config = shared_state_->config;
        if (config &&
            (!shared_state_->worker_pool ||
             shared_state_->worker_pool->IsStopped())) {
            shared_state_->worker_pool.reset(
                new foundation::concurrent::ThreadPool(
                    config->worker_thread_count));
        }
        worker_pool = shared_state_->worker_pool;
    }

    if (!worker_pool) {
        return MakeInvalidState("AMQP bus module is not initialized");
    }
    if (!config) {
        return MakeInvalidState("AMQP bus configuration is unavailable");
    }

    foundation::base::Result<void> pool_start = worker_pool->Start();
    if (!pool_start.IsOk()) {
        return pool_start;
    }

    std::shared_ptr<AmqpBusSharedState> state = shared_state_;
    std::shared_ptr<AmqpConnectionDriver> driver =
        CreateAmqpConnectionDriver(
            *config,
            [state](const IncomingMessage& incoming,
                    const DeliveryContext& delivery) {
                MessageHandler handler;
                std::shared_ptr<AmqpConnectionDriver> driver_ref;
                std::shared_ptr<foundation::concurrent::ThreadPool> worker_pool_ref;
                bool stopping = false;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    stopping = state->stopping;
                    std::map<std::string, MessageHandler>::iterator it =
                        state->handlers.find(incoming.consumer_name);
                    if (it != state->handlers.end()) {
                        handler = it->second;
                    }
                    driver_ref = state->driver;
                    worker_pool_ref = state->worker_pool;
                }

                if (stopping) {
                    if (!delivery.auto_ack && driver_ref) {
                        LogResultIfError(
                            CompleteDeliveryAsyncWithDriver(
                                driver_ref,
                                delivery,
                                ConsumeAction::Requeue),
                            "Requeue message because AMQP bus is stopping");
                    }
                    return;
                }

                if (!worker_pool_ref) {
                    if (!delivery.auto_ack && driver_ref) {
                        LogResultIfError(
                            CompleteDeliveryAsyncWithDriver(
                                driver_ref,
                                delivery,
                                ConsumeAction::Requeue),
                            "Requeue message because worker pool is unavailable");
                    }
                    return;
                }

                foundation::base::Result<std::future<void> > submitted =
                    worker_pool_ref->Submit(
                        [incoming, delivery, handler, driver_ref]() {
                            ConsumeAction action = ConsumeAction::Requeue;
                            if (handler) {
                                try {
                                    action = handler(incoming);
                                } catch (...) {
                                    FOUNDATION_LOG_ERROR(
                                        "Message handler for consumer '"
                                        << incoming.consumer_name
                                        << "' threw an exception");
                                    action = ConsumeAction::Requeue;
                                }
                            } else {
                                FOUNDATION_LOG_WARNING(
                                    "No message handler registered for consumer '"
                                    << incoming.consumer_name
                                    << "', requeueing message");
                            }

                            if (!delivery.auto_ack && driver_ref) {
                                LogResultIfError(
                                    CompleteDeliveryAsyncWithDriver(
                                        driver_ref,
                                        delivery,
                                        action),
                                    "Failed to complete delivery for consumer '" +
                                        incoming.consumer_name + "'");
                            }
                        });

                if (!submitted.IsOk() && !delivery.auto_ack && driver_ref) {
                    LogResultIfError(
                        CompleteDeliveryAsyncWithDriver(
                            driver_ref,
                            delivery,
                            ConsumeAction::Requeue),
                        "Requeue message because worker task submission failed");
                }
            },
            [state](const ConnectionStateChange& change) {
                DispatchConnectionStateChange(state, change);
            });

    {
        std::lock_guard<std::mutex> lock(shared_state_->mutex);
        shared_state_->driver = driver;
    }

    foundation::base::Result<void> start_result = StartDriver(driver);
    if (!start_result.IsOk()) {
        {
            std::lock_guard<std::mutex> lock(shared_state_->mutex);
            shared_state_->driver.reset();
        }
        (void)worker_pool->ShutdownNow();
        return start_result;
    }

    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> AmqpBusModule::OnStop() {
    foundation::base::Result<void> first_error = foundation::base::MakeSuccess();
    std::shared_ptr<AmqpConnectionDriver> driver;
    std::shared_ptr<foundation::concurrent::ThreadPool> worker_pool;
    {
        std::lock_guard<std::mutex> lock(shared_state_->mutex);
        shared_state_->stopping = true;
        driver = shared_state_->driver;
        worker_pool = shared_state_->worker_pool;
    }

    // 先等待已提交的业务 handler 结束，再停止 AMQP driver。handler 返回后仍可
    // 通过 driver 发送 ACK/NACK，降低停止过程中的重复投递风险。
    if (worker_pool) {
        foundation::base::Result<void> pool_result = worker_pool->Shutdown();
        if (first_error.IsOk() && !pool_result.IsOk()) {
            first_error = pool_result;
        }
    }

    foundation::base::Result<void> stop_result = StopDriver(driver);
    if (first_error.IsOk() && !stop_result.IsOk()) {
        first_error = stop_result;
    }

    {
        std::lock_guard<std::mutex> lock(shared_state_->mutex);
        shared_state_->handlers.clear();
    }

    return first_error;
}

foundation::base::Result<void> AmqpBusModule::OnFini() {
    std::lock_guard<std::mutex> lock(shared_state_->mutex);
    shared_state_->stopping = false;
    shared_state_->handlers.clear();
    shared_state_->state_handlers.clear();
    shared_state_->driver.reset();
    shared_state_->worker_pool.reset();
    shared_state_->config.reset(new AmqpBusConfig());
    return foundation::base::MakeSuccess();
}

MC_DECLARE_MODULE_FACTORY(AmqpBusModule)

}  // namespace messaging
}  // namespace module_context
