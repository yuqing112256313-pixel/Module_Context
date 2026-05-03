#include "AmqpBusModule.h"

#include "module_context/framework/IContext.h"
#include "module_context/framework/IModuleManager.h"
#include "module_context/messaging/IMessageBusService.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/config/ConfigValue.h"

#include <cstdint>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {

using foundation::config::ConfigValue;
using module_context::messaging::ConnectionState;
using module_context::messaging::ConsumeAction;
using module_context::messaging::IncomingMessage;
using module_context::messaging::IMessageBusService;
using module_context::messaging::MessageBusFeature;
using module_context::messaging::PublishRequest;
using module_context::messaging::PublishConfirmOptions;
using module_context::messaging::AmqpBusModule;

bool Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

void SetField(ConfigValue* object,
              const std::string& key,
              const ConfigValue& value) {
    (void)object->Set(key, value);
}

void AppendValue(ConfigValue* array, const ConfigValue& value) {
    (void)array->Append(value);
}

ConfigValue MakeExchange(const std::string& name, const std::string& type) {
    ConfigValue exchange = ConfigValue::MakeObject();
    SetField(&exchange, "name", ConfigValue(name));
    SetField(&exchange, "type", ConfigValue(type));
    SetField(&exchange, "durable", ConfigValue(true));
    return exchange;
}

ConfigValue MakeQueue(const std::string& name) {
    ConfigValue queue = ConfigValue::MakeObject();
    SetField(&queue, "name", ConfigValue(name));
    SetField(&queue, "durable", ConfigValue(true));
    return queue;
}

ConfigValue MakeBinding(const std::string& exchange,
                        const std::string& queue,
                        const std::string& routing_key) {
    ConfigValue binding = ConfigValue::MakeObject();
    SetField(&binding, "exchange", ConfigValue(exchange));
    SetField(&binding, "queue", ConfigValue(queue));
    SetField(&binding, "routing_key", ConfigValue(routing_key));
    return binding;
}

ConfigValue MakePublisher(const std::string& name,
                          const std::string& exchange,
                          const std::string& routing_key) {
    ConfigValue publisher = ConfigValue::MakeObject();
    SetField(&publisher, "name", ConfigValue(name));
    SetField(&publisher, "exchange", ConfigValue(exchange));
    SetField(&publisher, "routing_key", ConfigValue(routing_key));
    SetField(&publisher, "content_type", ConfigValue("text/plain"));
    SetField(&publisher, "content_encoding", ConfigValue("utf-8"));
    SetField(&publisher, "message_id", ConfigValue("publisher-template"));
    SetField(&publisher, "type", ConfigValue("module-context-test"));
    SetField(&publisher, "app_id", ConfigValue("amqp_bus_module_test"));
    SetField(&publisher, "priority", ConfigValue(static_cast<std::int64_t>(3)));
    SetField(&publisher, "timestamp", ConfigValue(static_cast<std::int64_t>(1)));
    SetField(&publisher, "persistent", ConfigValue(true));
    return publisher;
}

ConfigValue MakeConsumer(const std::string& name,
                         const std::string& queue,
                         std::int64_t prefetch_count) {
    ConfigValue consumer = ConfigValue::MakeObject();
    SetField(&consumer, "name", ConfigValue(name));
    SetField(&consumer, "queue", ConfigValue(queue));
    SetField(&consumer, "prefetch_count", ConfigValue(prefetch_count));
    SetField(&consumer, "auto_ack", ConfigValue(false));
    return consumer;
}

ConfigValue MakeAmqpConfig(bool invalid_reference) {
    ConfigValue root = ConfigValue::MakeObject();

    ConfigValue connection = ConfigValue::MakeObject();
    SetField(&connection, "uri", ConfigValue("amqp://guest:guest@127.0.0.1:1/"));
    SetField(&connection, "heartbeat_seconds", ConfigValue(1));
    SetField(&connection, "connect_timeout_ms", ConfigValue(100));
    SetField(&connection, "socket_timeout_ms", ConfigValue(20));

    ConfigValue reconnect = ConfigValue::MakeObject();
    SetField(&reconnect, "enabled", ConfigValue(true));
    SetField(&reconnect, "initial_delay_ms", ConfigValue(10));
    SetField(&reconnect, "max_delay_ms", ConfigValue(20));
    SetField(&connection, "reconnect", reconnect);
    SetField(&root, "connection", connection);

    ConfigValue worker_pool = ConfigValue::MakeObject();
    SetField(&worker_pool, "thread_count", ConfigValue(1));
    SetField(&root, "worker_pool", worker_pool);

    ConfigValue topology = ConfigValue::MakeObject();
    ConfigValue exchanges = ConfigValue::MakeArray();
    AppendValue(&exchanges, MakeExchange("task.exchange", "direct"));
    SetField(&topology, "exchanges", exchanges);

    ConfigValue queues = ConfigValue::MakeArray();
    AppendValue(&queues, MakeQueue("task.queue"));
    SetField(&topology, "queues", queues);

    ConfigValue bindings = ConfigValue::MakeArray();
    AppendValue(
        &bindings,
        MakeBinding(
            invalid_reference ? "missing.exchange" : "task.exchange",
            "task.queue",
            "task.run"));
    SetField(&topology, "bindings", bindings);
    SetField(&root, "topology", topology);

    ConfigValue publishers = ConfigValue::MakeArray();
    AppendValue(&publishers, MakePublisher("task_publisher", "task.exchange", "task.run"));
    AppendValue(&publishers, MakePublisher("default_exchange_publisher", "", "task.queue"));
    SetField(&root, "publishers", publishers);

    ConfigValue consumers = ConfigValue::MakeArray();
    AppendValue(
        &consumers,
        MakeConsumer(
            "task_worker",
            invalid_reference ? "missing.queue" : "task.queue",
            1));
    SetField(&root, "consumers", consumers);

    return root;
}

class DummyModuleManager : public module_context::framework::IModuleManager {
public:
    explicit DummyModuleManager(const ConfigValue& module_config)
        : module_config_(module_config) {
    }

    foundation::base::Result<void> LoadModules(const std::string&) override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> LoadModule(
        const std::string&,
        const std::string&) override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> Init(module_context::framework::IContext&) override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> Start() override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> Stop() override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> Fini() override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<ConfigValue> ModuleConfig(
        const std::string& name) override {
        if (name != "amqp_bus") {
            return foundation::base::Result<ConfigValue>(
                foundation::base::ErrorCode::kNotFound,
                "Unknown module config");
        }

        return foundation::base::Result<ConfigValue>(module_config_);
    }

private:
    foundation::base::Result<module_context::framework::IModule*> LookupModuleRaw(
        const std::string&) override {
        return foundation::base::Result<module_context::framework::IModule*>(
            foundation::base::ErrorCode::kNotFound,
            "No test modules are registered");
    }

    ConfigValue module_config_;
};

class DummyContext : public module_context::framework::IContext {
public:
    explicit DummyContext(module_context::framework::IModuleManager* manager)
        : manager_(manager) {
    }

    foundation::base::Result<void> Init() override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> Start() override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> Stop() override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> Fini() override {
        return foundation::base::MakeSuccess();
    }

    module_context::framework::IModuleManager* ModuleManager() override {
        return manager_;
    }

private:
    foundation::base::Result<module_context::framework::IModule*> LookupServiceRaw(
        const char*,
        const std::string&) override {
        return foundation::base::Result<module_context::framework::IModule*>(
            foundation::base::ErrorCode::kNotFound,
            "No services are registered");
    }

    foundation::base::Result<module_context::framework::IModule*> LookupUniqueServiceRaw(
        const char*) override {
        return foundation::base::Result<module_context::framework::IModule*>(
            foundation::base::ErrorCode::kNotFound,
            "No services are registered");
    }

    module_context::framework::IModuleManager* manager_;
};

bool RunLifecycleCase() {
    DummyModuleManager manager(MakeAmqpConfig(false));
    DummyContext context(&manager);
    AmqpBusModule module;
    IMessageBusService* bus_api = &module;

    if (!Expect(bus_api != NULL, "AmqpBusModule should expose IMessageBusService")) {
        return false;
    }

    foundation::base::Result<void> init_result = module.Init(context);
    if (!Expect(init_result.IsOk(), "AmqpBusModule Init should succeed")) {
        return false;
    }

    if (!Expect(
            bus_api->GetConnectionState() == ConnectionState::Created,
            "Connection state should be Created after Init")) {
        return false;
    }

    foundation::base::Result<void> unknown_register =
        bus_api->RegisterConsumerHandler(
            "missing_consumer",
            [](const IncomingMessage&) { return ConsumeAction::Ack; });
    if (!Expect(
            !unknown_register.IsOk() &&
                unknown_register.GetError() == foundation::base::ErrorCode::kNotFound,
            "RegisterConsumerHandler should reject unknown consumers")) {
        return false;
    }

    foundation::base::Result<void> register_result =
        bus_api->RegisterConsumerHandler(
            "task_worker",
            [](const IncomingMessage&) { return ConsumeAction::Ack; });
    if (!Expect(register_result.IsOk(), "RegisterConsumerHandler should succeed")) {
        return false;
    }

    foundation::base::Result<void> unregister_result =
        bus_api->UnregisterConsumerHandler("task_worker");
    if (!Expect(unregister_result.IsOk(), "UnregisterConsumerHandler should succeed")) {
        return false;
    }

    if (!Expect(module.ModuleType() == "amqp_bus",
                "AmqpBusModule should expose the platform ModuleType")) {
        return false;
    }

    if (!Expect(
            bus_api->SupportsFeature(MessageBusFeature::PublisherConfirm) &&
                bus_api->SupportsFeature(MessageBusFeature::MandatoryReturn),
            "AMQP bus should report publisher confirm and mandatory return support")) {
        return false;
    }

    PublishRequest request;
    request.exchange = "task.exchange";
    request.routing_key = "task.run";
    request.payload.push_back('x');
    request.properties.content_type = "text/plain";
    request.properties.content_encoding = "utf-8";
    request.properties.message_id = "publish-property-smoke";
    request.properties.type = "module-context-test";
    request.properties.app_id = "amqp_bus_module_test";
    request.properties.has_priority = true;
    request.properties.priority = 3;
    request.properties.has_timestamp = true;
    request.properties.timestamp = 1;

    foundation::base::Result<void> publish_before_start = bus_api->Publish(request);
    if (!Expect(
            !publish_before_start.IsOk() &&
                publish_before_start.GetError() ==
                    foundation::base::ErrorCode::kDisconnected,
            "Publish before Start should return kDisconnected")) {
        return false;
    }

    foundation::base::Result<void> publish_async_before_start =
        bus_api->PublishAsync(request);
    if (!Expect(
            !publish_async_before_start.IsOk() &&
                publish_async_before_start.GetError() ==
                    foundation::base::ErrorCode::kDisconnected,
            "PublishAsync before Start should return kDisconnected")) {
        return false;
    }

    PublishConfirmOptions confirm_options;
    foundation::base::Result<module_context::messaging::PublishReceipt>
        confirmed_before_start =
            bus_api->PublishConfirmed(request, confirm_options);
    if (!Expect(
            !confirmed_before_start.IsOk() &&
                confirmed_before_start.GetError() ==
                    foundation::base::ErrorCode::kDisconnected,
            "PublishConfirmed before Start should return kDisconnected")) {
        return false;
    }

    PublishRequest default_exchange_request;
    default_exchange_request.routing_key = "task.queue";
    default_exchange_request.payload.push_back('d');
    foundation::base::Result<void> default_publish_before_start =
        bus_api->Publish(default_exchange_request);
    if (!Expect(
            !default_publish_before_start.IsOk() &&
                default_publish_before_start.GetError() ==
                    foundation::base::ErrorCode::kDisconnected,
            "Publish through AMQP default exchange should be accepted by validation")) {
        return false;
    }

    foundation::base::Result<void> start_result = module.Start();
    if (!Expect(start_result.IsOk(), "AmqpBusModule Start should succeed")) {
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    const ConnectionState state_after_start = bus_api->GetConnectionState();
    if (!Expect(
            state_after_start == ConnectionState::Connecting ||
                state_after_start == ConnectionState::Reconnecting ||
                state_after_start == ConnectionState::Connected,
            "Connection state should leave Created after Start")) {
        return false;
    }

    foundation::base::Result<void> publish_after_start = bus_api->Publish(request);
    if (!Expect(
            !publish_after_start.IsOk() &&
                publish_after_start.GetError() ==
                    foundation::base::ErrorCode::kDisconnected,
            "Publish while broker is unavailable should return kDisconnected")) {
        return false;
    }

    foundation::base::Result<void> publish_async_after_start =
        bus_api->PublishAsync(request);
    if (!Expect(
            !publish_async_after_start.IsOk() &&
                publish_async_after_start.GetError() ==
                    foundation::base::ErrorCode::kDisconnected,
            "PublishAsync while broker is unavailable should return kDisconnected")) {
        return false;
    }

    foundation::base::Result<module_context::messaging::PublishReceipt>
        confirmed_after_start =
            bus_api->PublishConfirmed(request, confirm_options);
    if (!Expect(
            !confirmed_after_start.IsOk() &&
                confirmed_after_start.GetError() ==
                    foundation::base::ErrorCode::kDisconnected,
            "PublishConfirmed while broker is unavailable should return kDisconnected")) {
        return false;
    }

    foundation::base::Result<void> stop_result = module.Stop();
    if (!Expect(stop_result.IsOk(), "AmqpBusModule Stop should succeed")) {
        return false;
    }

    foundation::base::Result<void> restart_result = module.Start();
    if (!Expect(restart_result.IsOk(), "AmqpBusModule should restart after Stop")) {
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    foundation::base::Result<void> second_stop_result = module.Stop();
    if (!Expect(second_stop_result.IsOk(), "AmqpBusModule second Stop should succeed")) {
        return false;
    }

    foundation::base::Result<void> fini_result = module.Fini();
    if (!Expect(fini_result.IsOk(), "AmqpBusModule Fini should succeed")) {
        return false;
    }

    return true;
}

bool RunInvalidReferenceCase() {
    DummyModuleManager manager(MakeAmqpConfig(true));
    DummyContext context(&manager);
    AmqpBusModule module;

    foundation::base::Result<void> init_result = module.Init(context);
    return Expect(
        !init_result.IsOk() &&
            init_result.GetError() == foundation::base::ErrorCode::kParseError,
        "AmqpBusModule should reject configs with invalid topology references");
}

}  // namespace

int main() {
    if (!RunLifecycleCase()) {
        return 1;
    }
    if (!RunInvalidReferenceCase()) {
        return 1;
    }

    std::cout << "[PASSED] amqp_bus_module_test" << std::endl;
    return 0;
}
