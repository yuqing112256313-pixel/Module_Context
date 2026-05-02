#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "RabbitMqBusModule.h"
#include "internal/ConfigTypes.h"
#include "internal/DriverOps.h"
#include "internal/MessageBusServiceProxy.h"
#include "internal/SharedState.h"

#include "module_context/framework/IModuleManager.h"

#include "module_context/plugin/ModuleFactory.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/base/NonCopyable.h"
#include "foundation/base/Platform.h"
#include "foundation/concurrent/ThreadPool.h"
#include "foundation/log/Logger.h"

#include <amqpcpp.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#if FOUNDATION_PLATFORM_WINDOWS
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef GetMessage
#undef GetMessage
#endif
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace module_context {
namespace messaging {

namespace {

#if FOUNDATION_PLATFORM_WINDOWS
typedef SOCKET NativeSocket;
const NativeSocket kInvalidSocket = INVALID_SOCKET;
typedef int SocketOptionLength;
#else
typedef int NativeSocket;
const NativeSocket kInvalidSocket = -1;
typedef socklen_t SocketOptionLength;
#endif

struct PendingResult : private foundation::base::NonCopyable {
    std::promise<foundation::base::Result<void> > promise;
    std::mutex mutex;
    bool completed;

    PendingResult()
        : promise(),
          mutex(),
          completed(false) {
    }
};

struct PendingPublishResult : private foundation::base::NonCopyable {
    typedef std::shared_ptr<foundation::base::Result<PublishReceipt> > ResultPtr;

    std::promise<ResultPtr> promise;
    std::mutex mutex;
    bool completed;

    PendingPublishResult()
        : promise(),
          mutex(),
          completed(false) {
    }
};

struct PendingPublishConfirm {
    PublishRequest request;
    std::shared_ptr<PendingPublishResult> result;
    bool returned;
    int reply_code;
    std::string reply_text;

    PendingPublishConfirm()
        : request(),
          result(),
          returned(false),
          reply_code(0),
          reply_text() {
    }
};

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

std::string MakeErrorMessage(const std::string& prefix,
                             const std::string& detail) {
    if (detail.empty()) {
        return prefix;
    }

    return prefix + ": " + detail;
}

foundation::base::Result<void> MakeInvalidArgument(const std::string& message) {
    return foundation::base::Result<void>(
        foundation::base::ErrorCode::kInvalidArgument,
        message);
}

foundation::base::Result<void> MakeInvalidState(const std::string& message) {
    return foundation::base::Result<void>(
        foundation::base::ErrorCode::kInvalidState,
        message);
}

foundation::base::Result<void> MakeDisconnected(const std::string& message) {
    return foundation::base::Result<void>(
        foundation::base::ErrorCode::kDisconnected,
        message);
}

foundation::base::Result<void> ValidatePublishTarget(
    const std::string& exchange,
    const std::string& routing_key) {
    if (exchange.empty() && routing_key.empty()) {
        return MakeInvalidArgument(
            "Publish requires exchange or routing_key; "
            "AMQP default exchange requires routing_key as queue name");
    }

    return foundation::base::MakeSuccess();
}

void CompletePendingResult(
    const std::shared_ptr<PendingResult>& result,
    const foundation::base::Result<void>& value) {
    if (!result) {
        return;
    }

    std::lock_guard<std::mutex> lock(result->mutex);
    if (result->completed) {
        return;
    }

    result->completed = true;
    result->promise.set_value(value);
}

foundation::base::Result<void> WaitPendingResult(
    const std::shared_ptr<PendingResult>& result) {
    if (!result) {
        return MakeInvalidArgument("Pending result is unavailable");
    }

    return result->promise.get_future().get();
}

void CompletePendingPublishResult(
    const std::shared_ptr<PendingPublishResult>& result,
    const foundation::base::Result<PublishReceipt>& value) {
    if (!result) {
        return;
    }

    std::lock_guard<std::mutex> lock(result->mutex);
    if (result->completed) {
        return;
    }

    result->completed = true;
    result->promise.set_value(
        PendingPublishResult::ResultPtr(
            new foundation::base::Result<PublishReceipt>(value)));
}

foundation::base::Result<PublishReceipt> WaitPendingPublishResult(
    const std::shared_ptr<PendingPublishResult>& result,
    int timeout_ms) {
    if (!result) {
        return foundation::base::Result<PublishReceipt>(
            foundation::base::ErrorCode::kInvalidArgument,
            "Pending publish result is unavailable");
    }

    std::future<PendingPublishResult::ResultPtr> future =
        result->promise.get_future();
    if (timeout_ms > 0) {
        const std::future_status status =
            future.wait_for(std::chrono::milliseconds(timeout_ms));
        if (status != std::future_status::ready) {
            return foundation::base::Result<PublishReceipt>(
                foundation::base::ErrorCode::kTimeout,
                "Timed out waiting for AMQP publisher confirm");
        }
    }

    PendingPublishResult::ResultPtr value = future.get();
    if (!value) {
        return foundation::base::Result<PublishReceipt>(
            foundation::base::ErrorCode::kUnknown,
            "AMQP publisher confirm completed without result");
    }

    return *value;
}

foundation::base::Result<foundation::config::ConfigValue> GetRequiredObjectField(
    const foundation::config::ConfigValue& root,
    const std::string& key) {
    if (!root.IsObject()) {
        return foundation::base::Result<foundation::config::ConfigValue>(
            foundation::base::ErrorCode::kParseError,
            "Expected configuration object when reading '" + key + "'");
    }

    foundation::base::Result<foundation::config::ConfigValue> value =
        root.ObjectGet(key);
    if (!value.IsOk()) {
        return foundation::base::Result<foundation::config::ConfigValue>(
            foundation::base::ErrorCode::kParseError,
            "Missing required object field '" + key + "'");
    }

    if (!value.Value().IsObject()) {
        return foundation::base::Result<foundation::config::ConfigValue>(
            foundation::base::ErrorCode::kParseError,
            "Field '" + key + "' must be an object");
    }

    return value;
}

foundation::base::Result<foundation::config::ConfigValue> GetOptionalObjectField(
    const foundation::config::ConfigValue& root,
    const std::string& key) {
    if (!root.IsObject() || !root.Contains(key)) {
        return foundation::base::Result<foundation::config::ConfigValue>(
            foundation::config::ConfigValue::MakeObject());
    }

    foundation::base::Result<foundation::config::ConfigValue> value =
        root.ObjectGet(key);
    if (!value.IsOk()) {
        return foundation::base::Result<foundation::config::ConfigValue>(
            value.GetError(),
            value.GetMessage());
    }

    if (!value.Value().IsObject()) {
        return foundation::base::Result<foundation::config::ConfigValue>(
            foundation::base::ErrorCode::kParseError,
            "Field '" + key + "' must be an object");
    }

    return value;
}

foundation::base::Result<foundation::config::ConfigValue::Array> GetOptionalArrayField(
    const foundation::config::ConfigValue& root,
    const std::string& key) {
    if (!root.IsObject() || !root.Contains(key)) {
        return foundation::base::Result<foundation::config::ConfigValue::Array>(
            foundation::config::ConfigValue::Array());
    }

    foundation::base::Result<foundation::config::ConfigValue> value =
        root.ObjectGet(key);
    if (!value.IsOk()) {
        return foundation::base::Result<foundation::config::ConfigValue::Array>(
            value.GetError(),
            value.GetMessage());
    }

    if (!value.Value().IsArray()) {
        return foundation::base::Result<foundation::config::ConfigValue::Array>(
            foundation::base::ErrorCode::kParseError,
            "Field '" + key + "' must be an array");
    }

    return value.Value().AsArray();
}

foundation::base::Result<std::string> GetRequiredStringField(
    const foundation::config::ConfigValue& root,
    const std::string& key) {
    if (!root.IsObject()) {
        return foundation::base::Result<std::string>(
            foundation::base::ErrorCode::kParseError,
            "Expected configuration object when reading '" + key + "'");
    }

    foundation::base::Result<foundation::config::ConfigValue> value =
        root.ObjectGet(key);
    if (!value.IsOk()) {
        return foundation::base::Result<std::string>(
            foundation::base::ErrorCode::kParseError,
            "Missing required string field '" + key + "'");
    }

    foundation::base::Result<std::string> string_value = value.Value().AsString();
    if (!string_value.IsOk() || string_value.Value().empty()) {
        return foundation::base::Result<std::string>(
            foundation::base::ErrorCode::kParseError,
            "Field '" + key + "' must be a non-empty string");
    }

    return string_value;
}

foundation::base::Result<std::string> GetOptionalStringField(
    const foundation::config::ConfigValue& root,
    const std::string& key,
    const std::string& fallback) {
    if (!root.IsObject() || !root.Contains(key)) {
        return foundation::base::Result<std::string>(fallback);
    }

    foundation::base::Result<foundation::config::ConfigValue> value =
        root.ObjectGet(key);
    if (!value.IsOk()) {
        return foundation::base::Result<std::string>(
            value.GetError(),
            value.GetMessage());
    }

    foundation::base::Result<std::string> string_value = value.Value().AsString();
    if (!string_value.IsOk()) {
        return foundation::base::Result<std::string>(
            foundation::base::ErrorCode::kParseError,
            "Field '" + key + "' must be a string");
    }

    return string_value;
}

foundation::base::Result<bool> GetOptionalBoolField(
    const foundation::config::ConfigValue& root,
    const std::string& key,
    bool fallback) {
    if (!root.IsObject() || !root.Contains(key)) {
        return foundation::base::Result<bool>(fallback);
    }

    foundation::base::Result<foundation::config::ConfigValue> value =
        root.ObjectGet(key);
    if (!value.IsOk()) {
        return foundation::base::Result<bool>(value.GetError(), value.GetMessage());
    }

    foundation::base::Result<bool> bool_value = value.Value().AsBool();
    if (!bool_value.IsOk()) {
        return foundation::base::Result<bool>(
            foundation::base::ErrorCode::kParseError,
            "Field '" + key + "' must be a bool");
    }

    return bool_value;
}

foundation::base::Result<std::int64_t> GetOptionalInt64Field(
    const foundation::config::ConfigValue& root,
    const std::string& key,
    std::int64_t fallback) {
    if (!root.IsObject() || !root.Contains(key)) {
        return foundation::base::Result<std::int64_t>(fallback);
    }

    foundation::base::Result<foundation::config::ConfigValue> value =
        root.ObjectGet(key);
    if (!value.IsOk()) {
        return foundation::base::Result<std::int64_t>(
            value.GetError(),
            value.GetMessage());
    }

    foundation::base::Result<std::int64_t> int_value = value.Value().AsInt64();
    if (!int_value.IsOk()) {
        return foundation::base::Result<std::int64_t>(
            foundation::base::ErrorCode::kParseError,
            "Field '" + key + "' must be an int64");
    }

    return int_value;
}

foundation::base::Result<foundation::config::ConfigValue::Object> GetOptionalArgumentsField(
    const foundation::config::ConfigValue& root,
    const std::string& key) {
    if (!root.IsObject() || !root.Contains(key)) {
        return foundation::base::Result<foundation::config::ConfigValue::Object>(
            foundation::config::ConfigValue::Object());
    }

    foundation::base::Result<foundation::config::ConfigValue> value =
        root.ObjectGet(key);
    if (!value.IsOk()) {
        return foundation::base::Result<foundation::config::ConfigValue::Object>(
            value.GetError(),
            value.GetMessage());
    }

    if (!value.Value().IsObject()) {
        return foundation::base::Result<foundation::config::ConfigValue::Object>(
            foundation::base::ErrorCode::kParseError,
            "Field '" + key + "' must be an object");
    }

    return value.Value().AsObject();
}

foundation::base::Result<ExchangeType> ParseExchangeTypeValue(
    const std::string& value) {
    if (value == "direct") {
        return foundation::base::Result<ExchangeType>(ExchangeType::Direct);
    }
    if (value == "fanout") {
        return foundation::base::Result<ExchangeType>(ExchangeType::Fanout);
    }
    if (value == "topic") {
        return foundation::base::Result<ExchangeType>(ExchangeType::Topic);
    }
    if (value == "headers") {
        return foundation::base::Result<ExchangeType>(ExchangeType::Headers);
    }

    return foundation::base::Result<ExchangeType>(
        foundation::base::ErrorCode::kParseError,
        "Unsupported exchange type '" + value + "'");
}

AMQP::ExchangeType ToAmqpExchangeType(ExchangeType type) {
    switch (type) {
        case ExchangeType::Direct:
            return AMQP::direct;
        case ExchangeType::Fanout:
            return AMQP::fanout;
        case ExchangeType::Topic:
            return AMQP::topic;
        case ExchangeType::Headers:
            return AMQP::headers;
        default:
            return AMQP::direct;
    }
}

int ToExchangeFlags(const ExchangeSpec& spec) {
    int flags = 0;
    if (spec.durable) {
        flags |= AMQP::durable;
    }
    if (spec.auto_delete) {
        flags |= AMQP::autodelete;
    }
    if (spec.passive) {
        flags |= AMQP::passive;
    }
    if (spec.internal) {
        flags |= AMQP::internal;
    }
    return flags;
}

int ToQueueFlags(const QueueSpec& spec) {
    int flags = 0;
    if (spec.durable) {
        flags |= AMQP::durable;
    }
    if (spec.auto_delete) {
        flags |= AMQP::autodelete;
    }
    if (spec.passive) {
        flags |= AMQP::passive;
    }
    if (spec.exclusive) {
        flags |= AMQP::exclusive;
    }
    return flags;
}

int ToConsumeFlags(const ConsumerSpec& spec) {
    int flags = 0;
    if (spec.auto_ack) {
        flags |= AMQP::noack;
    }
    if (spec.exclusive) {
        flags |= AMQP::exclusive;
    }
    if (spec.no_local) {
        flags |= AMQP::nolocal;
    }
    return flags;
}

foundation::base::Result<AMQP::Table> ConfigObjectToAmqpTable(
    const foundation::config::ConfigValue::Object& object);
foundation::base::Result<AMQP::Array> ConfigArrayToAmqpArray(
    const foundation::config::ConfigValue::Array& array);
foundation::base::Result<std::unique_ptr<AMQP::Field> > ConfigValueToAmqpField(
    const foundation::config::ConfigValue& value);
foundation::base::Result<foundation::config::ConfigValue> AmqpFieldToConfigValue(
    const AMQP::Field& field);

foundation::base::Result<std::unique_ptr<AMQP::Field> > ConfigValueToAmqpField(
    const foundation::config::ConfigValue& value) {
    // 配置中的 arguments/headers 使用框架通用 ConfigValue 表达。这里集中转换成
    // AMQP table field，避免调用方或配置解析层直接依赖 AMQP-CPP 类型。
    switch (value.GetType()) {
        case foundation::config::ConfigValue::Type::kNull:
            return foundation::base::Result<std::unique_ptr<AMQP::Field> >(
                std::unique_ptr<AMQP::Field>(new AMQP::VoidField()));

        case foundation::config::ConfigValue::Type::kBool:
            return foundation::base::Result<std::unique_ptr<AMQP::Field> >(
                std::unique_ptr<AMQP::Field>(
                    new AMQP::BooleanSet(value.AsBool().Value())));

        case foundation::config::ConfigValue::Type::kInt64:
            return foundation::base::Result<std::unique_ptr<AMQP::Field> >(
                std::unique_ptr<AMQP::Field>(
                    new AMQP::LongLong(value.AsInt64().Value())));

        case foundation::config::ConfigValue::Type::kDouble:
            return foundation::base::Result<std::unique_ptr<AMQP::Field> >(
                std::unique_ptr<AMQP::Field>(
                    new AMQP::Double(value.AsDouble().Value())));

        case foundation::config::ConfigValue::Type::kString:
            return foundation::base::Result<std::unique_ptr<AMQP::Field> >(
                std::unique_ptr<AMQP::Field>(
                    new AMQP::LongString(value.AsString().Value())));

        case foundation::config::ConfigValue::Type::kArray: {
            foundation::base::Result<AMQP::Array> converted =
                ConfigArrayToAmqpArray(value.AsArray().Value());
            if (!converted.IsOk()) {
                return foundation::base::Result<std::unique_ptr<AMQP::Field> >(
                    converted.GetError(),
                    converted.GetMessage());
            }

            return foundation::base::Result<std::unique_ptr<AMQP::Field> >(
                std::unique_ptr<AMQP::Field>(
                    new AMQP::Array(converted.Value())));
        }

        case foundation::config::ConfigValue::Type::kObject: {
            foundation::base::Result<AMQP::Table> converted =
                ConfigObjectToAmqpTable(value.AsObject().Value());
            if (!converted.IsOk()) {
                return foundation::base::Result<std::unique_ptr<AMQP::Field> >(
                    converted.GetError(),
                    converted.GetMessage());
            }

            return foundation::base::Result<std::unique_ptr<AMQP::Field> >(
                std::unique_ptr<AMQP::Field>(
                    new AMQP::Table(converted.Value())));
        }

        default:
            return foundation::base::Result<std::unique_ptr<AMQP::Field> >(
                foundation::base::ErrorCode::kParseError,
                "Unsupported config value type for AMQP conversion");
    }
}

foundation::base::Result<AMQP::Array> ConfigArrayToAmqpArray(
    const foundation::config::ConfigValue::Array& array) {
    AMQP::Array converted;

    for (std::size_t index = 0; index < array.size(); ++index) {
        foundation::base::Result<std::unique_ptr<AMQP::Field> > field =
            ConfigValueToAmqpField(array[index]);
        if (!field.IsOk()) {
            return foundation::base::Result<AMQP::Array>(
                field.GetError(),
                field.GetMessage());
        }

        converted.push_back(*field.Value());
    }

    return foundation::base::Result<AMQP::Array>(converted);
}

foundation::base::Result<AMQP::Table> ConfigObjectToAmqpTable(
    const foundation::config::ConfigValue::Object& object) {
    AMQP::Table converted;

    for (foundation::config::ConfigValue::Object::const_iterator it = object.begin();
         it != object.end();
         ++it) {
        foundation::base::Result<std::unique_ptr<AMQP::Field> > field =
            ConfigValueToAmqpField(it->second);
        if (!field.IsOk()) {
            return foundation::base::Result<AMQP::Table>(
                field.GetError(),
                field.GetMessage());
        }

        converted.set(it->first, *field.Value());
    }

    return foundation::base::Result<AMQP::Table>(converted);
}

foundation::base::Result<foundation::config::ConfigValue> AmqpFieldToConfigValue(
    const AMQP::Field& field) {
    if (field.isVoid()) {
        return foundation::base::Result<foundation::config::ConfigValue>(
            foundation::config::ConfigValue());
    }

    if (field.isBoolean()) {
        const AMQP::BooleanSet& bool_field =
            static_cast<const AMQP::BooleanSet&>(field);
        return foundation::base::Result<foundation::config::ConfigValue>(
            foundation::config::ConfigValue(bool_field.value() != 0));
    }

    if (field.isString()) {
        const std::string& string_value = static_cast<const std::string&>(field);
        return foundation::base::Result<foundation::config::ConfigValue>(
            foundation::config::ConfigValue(string_value));
    }

    if (field.isArray()) {
        const AMQP::Array& array = static_cast<const AMQP::Array&>(field);
        foundation::config::ConfigValue value =
            foundation::config::ConfigValue::MakeArray();
        for (uint32_t index = 0; index < array.count(); ++index) {
            foundation::base::Result<foundation::config::ConfigValue> item =
                AmqpFieldToConfigValue(array[index]);
            if (!item.IsOk()) {
                return item;
            }
            foundation::base::Result<void> append_result =
                value.Append(item.Value());
            if (!append_result.IsOk()) {
                return foundation::base::Result<foundation::config::ConfigValue>(
                    append_result.GetError(),
                    append_result.GetMessage());
            }
        }
        return foundation::base::Result<foundation::config::ConfigValue>(value);
    }

    if (field.isTable()) {
        const AMQP::Table& table = static_cast<const AMQP::Table&>(field);
        foundation::config::ConfigValue value =
            foundation::config::ConfigValue::MakeObject();
        std::vector<std::string> keys = table.keys();
        for (std::size_t index = 0; index < keys.size(); ++index) {
            foundation::base::Result<foundation::config::ConfigValue> item =
                AmqpFieldToConfigValue(table.get(keys[index]));
            if (!item.IsOk()) {
                return item;
            }
            foundation::base::Result<void> set_result =
                value.Set(keys[index], item.Value());
            if (!set_result.IsOk()) {
                return foundation::base::Result<foundation::config::ConfigValue>(
                    set_result.GetError(),
                    set_result.GetMessage());
            }
        }
        return foundation::base::Result<foundation::config::ConfigValue>(value);
    }

    if (field.isDecimal() || field.typeID() == 'f' || field.typeID() == 'd') {
        return foundation::base::Result<foundation::config::ConfigValue>(
            foundation::config::ConfigValue(static_cast<double>(field)));
    }

    if (field.isInteger()) {
        return foundation::base::Result<foundation::config::ConfigValue>(
            foundation::config::ConfigValue(
                static_cast<std::int64_t>(static_cast<int64_t>(field))));
    }

    std::ostringstream oss;
    oss << field;
    return foundation::base::Result<foundation::config::ConfigValue>(
        foundation::config::ConfigValue(oss.str()));
}

foundation::base::Result<foundation::config::ConfigValue::Object> AmqpTableToConfigObject(
    const AMQP::Table& table) {
    foundation::config::ConfigValue::Object object;
    std::vector<std::string> keys = table.keys();

    for (std::size_t index = 0; index < keys.size(); ++index) {
        foundation::base::Result<foundation::config::ConfigValue> value =
            AmqpFieldToConfigValue(table.get(keys[index]));
        if (!value.IsOk()) {
            return foundation::base::Result<foundation::config::ConfigValue::Object>(
                value.GetError(),
                value.GetMessage());
        }
        object[keys[index]] = value.Value();
    }

    return foundation::base::Result<foundation::config::ConfigValue::Object>(
        object);
}

foundation::base::Result<ExchangeSpec> ParseExchangeSpec(
    const foundation::config::ConfigValue& value) {
    foundation::base::Result<std::string> name =
        GetRequiredStringField(value, "name");
    if (!name.IsOk()) {
        return foundation::base::Result<ExchangeSpec>(
            name.GetError(),
            name.GetMessage());
    }

    foundation::base::Result<std::string> type_string =
        GetOptionalStringField(value, "type", "direct");
    foundation::base::Result<ExchangeType> type =
        type_string.IsOk()
            ? ParseExchangeTypeValue(type_string.Value())
            : foundation::base::Result<ExchangeType>(
                type_string.GetError(),
                type_string.GetMessage());
    foundation::base::Result<bool> durable =
        GetOptionalBoolField(value, "durable", true);
    foundation::base::Result<bool> auto_delete =
        GetOptionalBoolField(value, "auto_delete", false);
    foundation::base::Result<bool> passive =
        GetOptionalBoolField(value, "passive", false);
    foundation::base::Result<bool> internal =
        GetOptionalBoolField(value, "internal", false);
    foundation::base::Result<foundation::config::ConfigValue::Object> arguments =
        GetOptionalArgumentsField(value, "arguments");

    if (!type.IsOk() || !durable.IsOk() || !auto_delete.IsOk() ||
        !passive.IsOk() || !internal.IsOk() || !arguments.IsOk()) {
        return foundation::base::Result<ExchangeSpec>(
            foundation::base::ErrorCode::kParseError,
            "Invalid exchange specification");
    }

    ExchangeSpec spec;
    spec.name = name.Value();
    spec.type = type.Value();
    spec.durable = durable.Value();
    spec.auto_delete = auto_delete.Value();
    spec.passive = passive.Value();
    spec.internal = internal.Value();
    spec.arguments = arguments.Value();
    return foundation::base::Result<ExchangeSpec>(spec);
}

foundation::base::Result<QueueSpec> ParseQueueSpec(
    const foundation::config::ConfigValue& value) {
    foundation::base::Result<std::string> name =
        GetRequiredStringField(value, "name");
    if (!name.IsOk()) {
        return foundation::base::Result<QueueSpec>(
            name.GetError(),
            name.GetMessage());
    }

    foundation::base::Result<bool> durable =
        GetOptionalBoolField(value, "durable", true);
    foundation::base::Result<bool> auto_delete =
        GetOptionalBoolField(value, "auto_delete", false);
    foundation::base::Result<bool> passive =
        GetOptionalBoolField(value, "passive", false);
    foundation::base::Result<bool> exclusive =
        GetOptionalBoolField(value, "exclusive", false);
    foundation::base::Result<foundation::config::ConfigValue::Object> arguments =
        GetOptionalArgumentsField(value, "arguments");

    if (!durable.IsOk() || !auto_delete.IsOk() || !passive.IsOk() ||
        !exclusive.IsOk() || !arguments.IsOk()) {
        return foundation::base::Result<QueueSpec>(
            foundation::base::ErrorCode::kParseError,
            "Invalid queue specification");
    }

    QueueSpec spec;
    spec.name = name.Value();
    spec.durable = durable.Value();
    spec.auto_delete = auto_delete.Value();
    spec.passive = passive.Value();
    spec.exclusive = exclusive.Value();
    spec.arguments = arguments.Value();
    return foundation::base::Result<QueueSpec>(spec);
}

foundation::base::Result<BindingSpec> ParseBindingSpec(
    const foundation::config::ConfigValue& value) {
    foundation::base::Result<std::string> exchange =
        GetRequiredStringField(value, "exchange");
    foundation::base::Result<std::string> queue =
        GetRequiredStringField(value, "queue");
    foundation::base::Result<std::string> routing_key =
        GetOptionalStringField(value, "routing_key", "");
    foundation::base::Result<foundation::config::ConfigValue::Object> arguments =
        GetOptionalArgumentsField(value, "arguments");

    if (!exchange.IsOk() || !queue.IsOk() || !routing_key.IsOk() ||
        !arguments.IsOk()) {
        return foundation::base::Result<BindingSpec>(
            foundation::base::ErrorCode::kParseError,
            "Invalid binding specification");
    }

    BindingSpec spec;
    spec.exchange = exchange.Value();
    spec.queue = queue.Value();
    spec.routing_key = routing_key.Value();
    spec.arguments = arguments.Value();
    return foundation::base::Result<BindingSpec>(spec);
}

foundation::base::Result<PublisherSpec> ParsePublisherSpec(
    const foundation::config::ConfigValue& value) {
    foundation::base::Result<std::string> name =
        GetRequiredStringField(value, "name");
    foundation::base::Result<std::string> exchange =
        GetOptionalStringField(value, "exchange", "");
    foundation::base::Result<std::string> routing_key =
        GetOptionalStringField(value, "routing_key", "");
    foundation::base::Result<std::string> content_type =
        GetOptionalStringField(value, "content_type", "");
    foundation::base::Result<bool> persistent =
        GetOptionalBoolField(value, "persistent", true);
    foundation::base::Result<foundation::config::ConfigValue::Object> headers =
        GetOptionalArgumentsField(value, "headers");

    if (!name.IsOk() || !exchange.IsOk() || !routing_key.IsOk() ||
        !content_type.IsOk() || !persistent.IsOk() || !headers.IsOk()) {
        return foundation::base::Result<PublisherSpec>(
            foundation::base::ErrorCode::kParseError,
            "Invalid publisher specification");
    }

    foundation::base::Result<void> target_result =
        ValidatePublishTarget(exchange.Value(), routing_key.Value());
    if (!target_result.IsOk()) {
        return foundation::base::Result<PublisherSpec>(
            target_result.GetError(),
            target_result.GetMessage());
    }

    PublisherSpec spec;
    spec.name = name.Value();
    spec.exchange = exchange.Value();
    spec.routing_key = routing_key.Value();
    spec.content_type = content_type.Value();
    spec.headers = headers.Value();
    spec.persistent = persistent.Value();
    return foundation::base::Result<PublisherSpec>(spec);
}

foundation::base::Result<ConsumerSpec> ParseConsumerSpec(
    const foundation::config::ConfigValue& value) {
    foundation::base::Result<std::string> name =
        GetRequiredStringField(value, "name");
    foundation::base::Result<std::string> queue =
        GetRequiredStringField(value, "queue");
    foundation::base::Result<std::string> consumer_tag =
        GetOptionalStringField(value, "consumer_tag", "");
    foundation::base::Result<bool> auto_ack =
        GetOptionalBoolField(value, "auto_ack", false);
    foundation::base::Result<bool> exclusive =
        GetOptionalBoolField(value, "exclusive", false);
    foundation::base::Result<bool> no_local =
        GetOptionalBoolField(value, "no_local", false);
    foundation::base::Result<std::int64_t> prefetch_count =
        GetOptionalInt64Field(value, "prefetch_count", 1);
    foundation::base::Result<foundation::config::ConfigValue::Object> arguments =
        GetOptionalArgumentsField(value, "arguments");

    if (!name.IsOk() || !queue.IsOk() || !consumer_tag.IsOk() ||
        !auto_ack.IsOk() || !exclusive.IsOk() || !no_local.IsOk() ||
        !prefetch_count.IsOk() || !arguments.IsOk()) {
        return foundation::base::Result<ConsumerSpec>(
            foundation::base::ErrorCode::kParseError,
            "Invalid consumer specification");
    }

    if (prefetch_count.Value() < 0 ||
        prefetch_count.Value() > std::numeric_limits<std::uint16_t>::max()) {
        return foundation::base::Result<ConsumerSpec>(
            foundation::base::ErrorCode::kParseError,
            "Consumer prefetch_count must be within uint16 range");
    }

    ConsumerSpec spec;
    spec.name = name.Value();
    spec.queue = queue.Value();
    spec.consumer_tag = consumer_tag.Value();
    spec.auto_ack = auto_ack.Value();
    spec.exclusive = exclusive.Value();
    spec.no_local = no_local.Value();
    spec.prefetch_count = static_cast<std::uint16_t>(prefetch_count.Value());
    spec.arguments = arguments.Value();
    return foundation::base::Result<ConsumerSpec>(spec);
}

template <typename T>
foundation::base::Result<void> ParseUniqueArray(
    const foundation::config::ConfigValue::Array& values,
    const std::string& collection_name,
    const std::function<foundation::base::Result<T>(const foundation::config::ConfigValue&)>& parser,
    const std::function<std::string(const T&)>& key_extractor,
    std::vector<T>* output) {
    if (output == NULL) {
        return MakeInvalidArgument("Output vector is null");
    }

    std::set<std::string> seen_keys;
    output->clear();
    output->reserve(values.size());

    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!values[index].IsObject()) {
            return foundation::base::Result<void>(
                foundation::base::ErrorCode::kParseError,
                collection_name + "[" +
                    std::to_string(static_cast<unsigned long long>(index)) +
                    "] must be an object");
        }

        foundation::base::Result<T> parsed = parser(values[index]);
        if (!parsed.IsOk()) {
            return foundation::base::Result<void>(
                parsed.GetError(),
                MakeErrorMessage(
                    "Failed to parse " + collection_name + "[" +
                        std::to_string(static_cast<unsigned long long>(index)) + "]",
                    parsed.GetMessage()));
        }

        const std::string key = key_extractor(parsed.Value());
        if (!seen_keys.insert(key).second) {
            return foundation::base::Result<void>(
                foundation::base::ErrorCode::kAlreadyExists,
                "Duplicate entry '" + key + "' in collection '" +
                    collection_name + "'");
        }

        output->push_back(parsed.Value());
    }

    return foundation::base::MakeSuccess();
}

foundation::base::Result<RabbitMqBusConfig> ParseBusConfig(
    const foundation::config::ConfigValue& config_value) {
    if (!config_value.IsObject()) {
        return foundation::base::Result<RabbitMqBusConfig>(
            foundation::base::ErrorCode::kParseError,
            "AMQP bus config must be an object");
    }

    RabbitMqBusConfig config;

    foundation::base::Result<foundation::config::ConfigValue> connection =
        GetRequiredObjectField(config_value, "connection");
    if (!connection.IsOk()) {
        return foundation::base::Result<RabbitMqBusConfig>(
            connection.GetError(),
            connection.GetMessage());
    }

    foundation::base::Result<std::string> uri =
        GetRequiredStringField(connection.Value(), "uri");
    foundation::base::Result<std::int64_t> heartbeat =
        GetOptionalInt64Field(connection.Value(), "heartbeat_seconds", 30);
    foundation::base::Result<std::int64_t> connect_timeout =
        GetOptionalInt64Field(connection.Value(), "connect_timeout_ms", 5000);
    foundation::base::Result<std::int64_t> socket_timeout =
        GetOptionalInt64Field(connection.Value(), "socket_timeout_ms", 100);
    foundation::base::Result<foundation::config::ConfigValue> reconnect =
        GetOptionalObjectField(connection.Value(), "reconnect");

    if (!uri.IsOk() || !heartbeat.IsOk() || !connect_timeout.IsOk() ||
        !socket_timeout.IsOk() || !reconnect.IsOk()) {
        return foundation::base::Result<RabbitMqBusConfig>(
            foundation::base::ErrorCode::kParseError,
            "Invalid connection configuration");
    }

    foundation::base::Result<bool> reconnect_enabled =
        GetOptionalBoolField(reconnect.Value(), "enabled", true);
    foundation::base::Result<std::int64_t> reconnect_initial =
        GetOptionalInt64Field(reconnect.Value(), "initial_delay_ms", 1000);
    foundation::base::Result<std::int64_t> reconnect_max =
        GetOptionalInt64Field(reconnect.Value(), "max_delay_ms", 30000);
    foundation::base::Result<foundation::config::ConfigValue> worker_pool =
        GetOptionalObjectField(config_value, "worker_pool");
    foundation::base::Result<std::int64_t> thread_count =
        GetOptionalInt64Field(worker_pool.Value(), "thread_count", 4);
    foundation::base::Result<foundation::config::ConfigValue> topology =
        GetOptionalObjectField(config_value, "topology");
    foundation::base::Result<foundation::config::ConfigValue> features =
        GetOptionalObjectField(config_value, "features");

    if (!reconnect_enabled.IsOk() || !reconnect_initial.IsOk() ||
        !reconnect_max.IsOk() || !worker_pool.IsOk() || !thread_count.IsOk() ||
        !topology.IsOk() || !features.IsOk()) {
        return foundation::base::Result<RabbitMqBusConfig>(
            foundation::base::ErrorCode::kParseError,
            "Invalid reconnect, worker_pool, or features configuration");
    }

    foundation::base::Result<bool> publisher_confirms_enabled =
        GetOptionalBoolField(features.Value(), "publisher_confirm", true);
    if (!publisher_confirms_enabled.IsOk()) {
        return foundation::base::Result<RabbitMqBusConfig>(
            foundation::base::ErrorCode::kParseError,
            "Invalid features.publisher_confirm configuration");
    }

    if (heartbeat.Value() < 0 ||
        heartbeat.Value() > std::numeric_limits<std::uint16_t>::max()) {
        return foundation::base::Result<RabbitMqBusConfig>(
            foundation::base::ErrorCode::kParseError,
            "connection.heartbeat_seconds must be within uint16 range");
    }

    if (connect_timeout.Value() < 10 || socket_timeout.Value() < 10 ||
        reconnect_initial.Value() < 0 ||
        reconnect_max.Value() < reconnect_initial.Value() ||
        thread_count.Value() <= 0) {
        return foundation::base::Result<RabbitMqBusConfig>(
            foundation::base::ErrorCode::kParseError,
            "Invalid timing or thread_count values in AMQP config");
    }

    config.connection.uri = uri.Value();
    config.connection.heartbeat_seconds =
        static_cast<std::uint16_t>(heartbeat.Value());
    config.connection.connect_timeout_ms =
        static_cast<int>(connect_timeout.Value());
    config.connection.socket_timeout_ms =
        static_cast<int>(socket_timeout.Value());
    config.connection.reconnect.enabled = reconnect_enabled.Value();
    config.connection.reconnect.initial_delay_ms =
        static_cast<int>(reconnect_initial.Value());
    config.connection.reconnect.max_delay_ms =
        static_cast<int>(reconnect_max.Value());
    config.worker_thread_count =
        static_cast<std::size_t>(thread_count.Value());
    config.publisher_confirms_enabled = publisher_confirms_enabled.Value();

    foundation::base::Result<foundation::config::ConfigValue::Array> exchanges =
        GetOptionalArrayField(topology.Value(), "exchanges");
    foundation::base::Result<foundation::config::ConfigValue::Array> queues =
        GetOptionalArrayField(topology.Value(), "queues");
    foundation::base::Result<foundation::config::ConfigValue::Array> bindings =
        GetOptionalArrayField(topology.Value(), "bindings");
    foundation::base::Result<foundation::config::ConfigValue::Array> publishers =
        GetOptionalArrayField(config_value, "publishers");
    foundation::base::Result<foundation::config::ConfigValue::Array> consumers =
        GetOptionalArrayField(config_value, "consumers");

    if (!exchanges.IsOk() || !queues.IsOk() || !bindings.IsOk() ||
        !publishers.IsOk() || !consumers.IsOk()) {
        return foundation::base::Result<RabbitMqBusConfig>(
            foundation::base::ErrorCode::kParseError,
            "AMQP arrays must be arrays when provided");
    }

    foundation::base::Result<void> parse_exchanges = ParseUniqueArray<ExchangeSpec>(
        exchanges.Value(),
        "topology.exchanges",
        ParseExchangeSpec,
        [](const ExchangeSpec& spec) { return spec.name; },
        &config.exchanges);
    foundation::base::Result<void> parse_queues = ParseUniqueArray<QueueSpec>(
        queues.Value(),
        "topology.queues",
        ParseQueueSpec,
        [](const QueueSpec& spec) { return spec.name; },
        &config.queues);
    foundation::base::Result<void> parse_bindings = ParseUniqueArray<BindingSpec>(
        bindings.Value(),
        "topology.bindings",
        ParseBindingSpec,
        [](const BindingSpec& spec) {
            return spec.exchange + "|" + spec.queue + "|" + spec.routing_key;
        },
        &config.bindings);
    foundation::base::Result<void> parse_publishers = ParseUniqueArray<PublisherSpec>(
        publishers.Value(),
        "publishers",
        ParsePublisherSpec,
        [](const PublisherSpec& spec) { return spec.name; },
        &config.publishers);
    foundation::base::Result<void> parse_consumers = ParseUniqueArray<ConsumerSpec>(
        consumers.Value(),
        "consumers",
        ParseConsumerSpec,
        [](const ConsumerSpec& spec) { return spec.name; },
        &config.consumers);

    const foundation::base::Result<void>* results[] = {
        &parse_exchanges, &parse_queues, &parse_bindings,
        &parse_publishers, &parse_consumers};
    for (std::size_t index = 0; index < sizeof(results) / sizeof(results[0]); ++index) {
        if (!results[index]->IsOk()) {
            return foundation::base::Result<RabbitMqBusConfig>(
                results[index]->GetError(),
                results[index]->GetMessage());
        }
    }

    std::set<std::string> exchange_names;
    for (std::size_t index = 0; index < config.exchanges.size(); ++index) {
        exchange_names.insert(config.exchanges[index].name);
    }

    std::set<std::string> queue_names;
    for (std::size_t index = 0; index < config.queues.size(); ++index) {
        queue_names.insert(config.queues[index].name);
    }

    for (std::size_t index = 0; index < config.bindings.size(); ++index) {
        const BindingSpec& binding = config.bindings[index];
        if (exchange_names.find(binding.exchange) == exchange_names.end()) {
            return foundation::base::Result<RabbitMqBusConfig>(
                foundation::base::ErrorCode::kParseError,
                "Binding '" + binding.exchange + "|" + binding.queue + "|" +
                    binding.routing_key + "' references unknown exchange '" +
                    binding.exchange + "'");
        }
        if (queue_names.find(binding.queue) == queue_names.end()) {
            return foundation::base::Result<RabbitMqBusConfig>(
                foundation::base::ErrorCode::kParseError,
                "Binding '" + binding.exchange + "|" + binding.queue + "|" +
                    binding.routing_key + "' references unknown queue '" +
                    binding.queue + "'");
        }
    }

    for (std::size_t index = 0; index < config.publishers.size(); ++index) {
        const PublisherSpec& publisher = config.publishers[index];
        if (!publisher.exchange.empty() &&
            exchange_names.find(publisher.exchange) == exchange_names.end()) {
            return foundation::base::Result<RabbitMqBusConfig>(
                foundation::base::ErrorCode::kParseError,
                "Publisher '" + publisher.name +
                    "' references unknown exchange '" +
                    publisher.exchange + "'");
        }
    }

    for (std::size_t index = 0; index < config.consumers.size(); ++index) {
        const ConsumerSpec& consumer = config.consumers[index];
        if (queue_names.find(consumer.queue) == queue_names.end()) {
            return foundation::base::Result<RabbitMqBusConfig>(
                foundation::base::ErrorCode::kParseError,
                "Consumer '" + consumer.name +
                    "' references unknown queue '" + consumer.queue + "'");
        }
    }

    return foundation::base::Result<RabbitMqBusConfig>(config);
}

bool IsSameBinding(const BindingSpec& lhs, const BindingSpec& rhs) {
    return lhs.exchange == rhs.exchange &&
           lhs.queue == rhs.queue &&
           lhs.routing_key == rhs.routing_key;
}

void UpsertExchangeSpec(const ExchangeSpec& spec,
                        std::vector<ExchangeSpec>* exchanges) {
    if (exchanges == NULL) {
        return;
    }

    for (std::size_t index = 0; index < exchanges->size(); ++index) {
        if ((*exchanges)[index].name == spec.name) {
            (*exchanges)[index] = spec;
            return;
        }
    }

    exchanges->push_back(spec);
}

void UpsertQueueSpec(const QueueSpec& spec,
                     std::vector<QueueSpec>* queues) {
    if (queues == NULL) {
        return;
    }

    for (std::size_t index = 0; index < queues->size(); ++index) {
        if ((*queues)[index].name == spec.name) {
            (*queues)[index] = spec;
            return;
        }
    }

    queues->push_back(spec);
}

void UpsertBindingSpec(const BindingSpec& spec,
                       std::vector<BindingSpec>* bindings) {
    if (bindings == NULL) {
        return;
    }

    for (std::size_t index = 0; index < bindings->size(); ++index) {
        if (IsSameBinding((*bindings)[index], spec)) {
            (*bindings)[index] = spec;
            return;
        }
    }

    bindings->push_back(spec);
}

void LogResultIfError(const foundation::base::Result<void>& result,
                      const std::string& message) {
    if (!result.IsOk()) {
        FOUNDATION_LOG_ERROR(message << ": " << result.GetMessage());
    }
}

bool IsSocketValid(NativeSocket socket_fd) {
    return socket_fd != kInvalidSocket;
}

std::string AddressFamilyName(int family) {
    switch (family) {
        case AF_INET:
            return "IPv4";
        case AF_INET6:
            return "IPv6";
        default:
            return "family=" + std::to_string(family);
    }
}

int GetLastSocketErrorCode() {
#if FOUNDATION_PLATFORM_WINDOWS
    return static_cast<int>(WSAGetLastError());
#else
    return errno;
#endif
}

std::string GetLastSocketErrorMessage(int error_code) {
#if FOUNDATION_PLATFORM_WINDOWS
    char* text = NULL;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                  FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD size = FormatMessageA(
        flags,
        NULL,
        static_cast<DWORD>(error_code),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&text),
        0,
        NULL);
    std::string message =
        size > 0 && text != NULL ? std::string(text, size) : "socket error";
    if (text != NULL) {
        LocalFree(text);
    }
    return message;
#else
    return std::strerror(error_code);
#endif
}

std::string FormatSocketError(int error_code) {
    std::ostringstream message;
    message << "socket_error=" << error_code << " ("
            << GetLastSocketErrorMessage(error_code) << ")";
    return message.str();
}

void CloseSocketHandle(NativeSocket* socket_fd) {
    if (socket_fd == NULL || !IsSocketValid(*socket_fd)) {
        return;
    }

#if FOUNDATION_PLATFORM_WINDOWS
    closesocket(*socket_fd);
#else
    close(*socket_fd);
#endif
    *socket_fd = kInvalidSocket;
}

bool SetSocketNonBlocking(NativeSocket socket_fd, bool non_blocking) {
#if FOUNDATION_PLATFORM_WINDOWS
    u_long mode = non_blocking ? 1UL : 0UL;
    return ioctlsocket(socket_fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }

    if (non_blocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(socket_fd, F_SETFL, flags) == 0;
#endif
}

bool SetSocketNoDelay(NativeSocket socket_fd) {
    int enabled = 1;
    return setsockopt(
        socket_fd,
        IPPROTO_TCP,
        TCP_NODELAY,
        reinterpret_cast<const char*>(&enabled),
        static_cast<SocketOptionLength>(sizeof(enabled))) == 0;
}

bool WouldBlockError(int error_code) {
#if FOUNDATION_PLATFORM_WINDOWS
    return error_code == WSAEWOULDBLOCK;
#else
    return error_code == EWOULDBLOCK || error_code == EAGAIN;
#endif
}

bool ConnectInProgressError(int error_code) {
#if FOUNDATION_PLATFORM_WINDOWS
    return error_code == WSAEWOULDBLOCK || error_code == WSAEINPROGRESS;
#else
    return error_code == EINPROGRESS;
#endif
}

class SocketSystemScope : private foundation::base::NonCopyable {
public:
    SocketSystemScope()
        : ok_(true) {
#if FOUNDATION_PLATFORM_WINDOWS
        WSADATA data;
        ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#endif
    }

    ~SocketSystemScope() {
#if FOUNDATION_PLATFORM_WINDOWS
        if (ok_) {
            WSACleanup();
        }
#endif
    }

    bool IsOk() const {
        return ok_;
    }

private:
    bool ok_;
};

struct ConsumerRuntime {
    ConsumerSpec spec;
    std::unique_ptr<AMQP::Channel> channel;
    std::string active_tag;
    bool active;

    ConsumerRuntime()
        : spec(),
          channel(),
          active_tag(),
          active(false) {
    }
};

}  // namespace

class RabbitMqConnectionDriver : public AMQP::ConnectionHandler,
                                 private foundation::base::NonCopyable {
public:
    typedef std::function<void(
        const IncomingMessage&,
        const DeliveryContext&)> DeliveryCallback;

public:
    RabbitMqConnectionDriver(const RabbitMqBusConfig& config,
                             DeliveryCallback delivery_callback);
    ~RabbitMqConnectionDriver();

    foundation::base::Result<void> Start();
    foundation::base::Result<void> Stop();
    foundation::base::Result<void> Publish(const PublishRequest& request);
    foundation::base::Result<void> PublishAsync(const PublishRequest& request);
    foundation::base::Result<PublishReceipt> PublishConfirmed(
        const PublishRequest& request,
        const PublishConfirmOptions& options);
    foundation::base::Result<void> DeclareExchange(const ExchangeSpec& spec);
    foundation::base::Result<void> DeclareQueue(const QueueSpec& spec);
    foundation::base::Result<void> BindQueue(const BindingSpec& spec);
    foundation::base::Result<void> CompleteDelivery(
        const DeliveryContext& delivery,
        ConsumeAction action);
    foundation::base::Result<void> CompleteDeliveryAsync(
        const DeliveryContext& delivery,
        ConsumeAction action);
    ConnectionState GetConnectionState() const;
    std::string LastError() const;

    void onData(AMQP::Connection* connection, const char* data, size_t size) override;
    void onReady(AMQP::Connection* connection) override;
    void onError(AMQP::Connection* connection, const char* message) override;
    void onClosed(AMQP::Connection* connection) override;
    std::uint16_t onNegotiate(AMQP::Connection* connection, std::uint16_t interval) override;

private:
    foundation::base::Result<void> EnqueueCommand(
        const std::function<void()>& command);
    void ThreadMain();
    bool ShouldStop() const;
    void SleepFor(std::chrono::milliseconds duration) const;
    bool ShouldAttemptReconnect();
    void ApplyReconnectBackoff(const std::string& error);
    foundation::base::Result<void> ConnectSocket();
    void FlushOutboundData();
    void PumpIncomingData();
    void MaybeSendHeartbeat();
    void DrainCommands();
    bool IsReadyLocked() const;
    void RequestDisconnect(const std::string& reason);
    void HandleDisconnect(const std::string& reason);
    void StartBootstrap();
    void EnablePublisherConfirms();
    void BootstrapExchanges(std::size_t index);
    void BootstrapQueues(std::size_t index);
    void BootstrapBindings(std::size_t index);
    void BootstrapConsumers(std::size_t index);
    void StartConsumer(std::size_t index);
    foundation::base::Result<void> FillEnvelope(
        const PublishRequest& request,
        AMQP::Envelope* envelope) const;
    int PublishFlags(const PublishRequest& request) const;
    void CompletePublishConfirm(std::uint64_t delivery_tag,
                                bool multiple,
                                PublishDisposition disposition,
                                int reply_code,
                                const std::string& reply_text);
    void FailPendingPublishes(const std::string& reason);
    bool MarkReturnedPublish(const AMQP::Message& message,
                             int16_t code,
                             const std::string& description);
    void HandleReturnedMessage(
        const AMQP::Message& message,
        int16_t code,
        const std::string& description);
    void HandleIncomingMessage(const ConsumerSpec& spec,
                               const AMQP::Message& message,
                               std::uint64_t delivery_tag,
                               bool redelivered);

private:
    RabbitMqBusConfig config_;
    DeliveryCallback delivery_callback_;
    mutable std::mutex mutex_;
    std::thread thread_;
    bool stop_requested_;
    ConnectionState state_;
    std::string last_error_;
    std::deque<std::function<void()> > commands_;
    std::unique_ptr<AMQP::Connection> connection_;
    std::unique_ptr<AMQP::Channel> admin_channel_;
    std::unique_ptr<AMQP::Channel> publish_channel_;
    std::map<std::string, ConsumerRuntime> consumer_runtimes_;
    NativeSocket socket_fd_;
    bool connected_;
    bool ready_;
    bool handshake_ready_;
    bool publisher_confirms_ready_;
    std::uint64_t next_publish_confirm_tag_;
    std::map<std::uint64_t, PendingPublishConfirm> pending_publish_confirms_;
    bool pending_disconnect_;
    std::string pending_disconnect_reason_;
    std::string outbound_;
    std::vector<char> inbound_;
    std::chrono::steady_clock::time_point next_reconnect_at_;
    int reconnect_delay_ms_;
    std::chrono::steady_clock::time_point last_heartbeat_sent_;
};

RabbitMqConnectionDriver::RabbitMqConnectionDriver(
    const RabbitMqBusConfig& config,
    DeliveryCallback delivery_callback)
    : config_(config),
      delivery_callback_(delivery_callback),
      mutex_(),
      thread_(),
      stop_requested_(false),
      state_(ConnectionState::Created),
      last_error_(),
      commands_(),
      connection_(),
      admin_channel_(),
      publish_channel_(),
      consumer_runtimes_(),
      socket_fd_(kInvalidSocket),
      connected_(false),
      ready_(false),
      handshake_ready_(false),
      publisher_confirms_ready_(false),
      next_publish_confirm_tag_(1),
      pending_publish_confirms_(),
      pending_disconnect_(false),
      pending_disconnect_reason_(),
      outbound_(),
      inbound_(),
      next_reconnect_at_(std::chrono::steady_clock::now()),
      reconnect_delay_ms_(config.connection.reconnect.initial_delay_ms),
      last_heartbeat_sent_(std::chrono::steady_clock::time_point::min()) {
}

RabbitMqConnectionDriver::~RabbitMqConnectionDriver() {
    (void)Stop();
}

foundation::base::Result<void> RabbitMqConnectionDriver::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_.joinable()) {
        return MakeInvalidState("AMQP connection driver already started");
    }

    stop_requested_ = false;
    state_ = ConnectionState::Connecting;
    last_error_.clear();
    next_reconnect_at_ = std::chrono::steady_clock::now();
    reconnect_delay_ms_ = config_.connection.reconnect.initial_delay_ms;
    thread_ = std::thread(&RabbitMqConnectionDriver::ThreadMain, this);
    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> RabbitMqConnectionDriver::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    state_ = ConnectionState::Stopped;
    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> RabbitMqConnectionDriver::FillEnvelope(
    const PublishRequest& request,
    AMQP::Envelope* envelope) const {
    if (envelope == NULL) {
        return MakeInvalidArgument("AMQP envelope output is null");
    }

    foundation::base::Result<AMQP::Table> headers =
        ConfigObjectToAmqpTable(request.headers);
    if (!headers.IsOk()) {
        return foundation::base::Result<void>(
            headers.GetError(),
            headers.GetMessage());
    }

    if (!request.content_type.empty()) {
        envelope->setContentType(request.content_type);
    }
    if (!request.correlation_id.empty()) {
        envelope->setCorrelationID(request.correlation_id);
    }
    if (!request.reply_to.empty()) {
        envelope->setReplyTo(request.reply_to);
    }
    if (!request.headers.empty()) {
        envelope->setHeaders(headers.Value());
    }
    envelope->setPersistent(request.persistent);
    return foundation::base::MakeSuccess();
}

int RabbitMqConnectionDriver::PublishFlags(const PublishRequest& request) const {
    return request.mandatory ? AMQP::mandatory : 0;
}

foundation::base::Result<void> RabbitMqConnectionDriver::Publish(
    const PublishRequest& request) {
    foundation::base::Result<void> target_result =
        ValidatePublishTarget(request.exchange, request.routing_key);
    if (!target_result.IsOk()) {
        return target_result;
    }

    std::shared_ptr<PendingResult> pending(new PendingResult());
    foundation::base::Result<void> enqueue = EnqueueCommand(
        [this, request, pending]() {
            if (!IsReadyLocked()) {
                CompletePendingResult(
                    pending,
                    MakeDisconnected("AMQP connection is not ready"));
                return;
            }

            if (!publish_channel_) {
                CompletePendingResult(
                    pending,
                    MakeDisconnected("AMQP publisher channel is unavailable"));
                return;
            }

            const char* payload = request.payload.empty() ? "" : &request.payload[0];
            AMQP::Envelope envelope(payload, request.payload.size());
            foundation::base::Result<void> envelope_result =
                FillEnvelope(request, &envelope);
            if (!envelope_result.IsOk()) {
                CompletePendingResult(
                    pending,
                    foundation::base::Result<void>(
                        envelope_result.GetError(),
                        envelope_result.GetMessage()));
                return;
            }

            bool published = publish_channel_->publish(
                    request.exchange,
                    request.routing_key,
                    envelope,
                    PublishFlags(request));
            if (!published) {
                CompletePendingResult(
                    pending,
                    foundation::base::Result<void>(
                        foundation::base::ErrorCode::kOperationFailed,
                        "AMQP publish command was rejected locally"));
                return;
            }

            if (publisher_confirms_ready_) {
                ++next_publish_confirm_tag_;
            }
            CompletePendingResult(pending, foundation::base::MakeSuccess());
        });

    if (!enqueue.IsOk()) {
        return enqueue;
    }

    return WaitPendingResult(pending);
}

foundation::base::Result<void> RabbitMqConnectionDriver::PublishAsync(
    const PublishRequest& request) {
    foundation::base::Result<void> target_result =
        ValidatePublishTarget(request.exchange, request.routing_key);
    if (!target_result.IsOk()) {
        return target_result;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!thread_.joinable() || stop_requested_) {
            return MakeInvalidState("AMQP connection driver is not running");
        }
        // 这里只读取受 mutex_ 保护的连接状态，避免从调用线程直接读取 AMQP channel
        // 指针等驱动线程字段。真正发布前还会在驱动线程中再次检查 IsReadyLocked()。
        if (state_ != ConnectionState::Connected) {
            return MakeDisconnected("AMQP connection is not ready");
        }
    }

    return EnqueueCommand([this, request]() {
        if (!IsReadyLocked()) {
            FOUNDATION_LOG_WARNING(
                "Dropped publish command because AMQP connection became unavailable");
            return;
        }

        if (!publish_channel_) {
            FOUNDATION_LOG_ERROR(
                "AMQP async publish failed because publisher channel is unavailable");
            return;
        }

        const char* payload = request.payload.empty() ? "" : &request.payload[0];
        AMQP::Envelope envelope(payload, request.payload.size());
        foundation::base::Result<void> envelope_result =
            FillEnvelope(request, &envelope);
        if (!envelope_result.IsOk()) {
            FOUNDATION_LOG_ERROR(
                "Failed to build publish envelope for exchange '"
                << request.exchange << "': " << envelope_result.GetMessage());
            return;
        }

        bool published = publish_channel_->publish(
                request.exchange,
                request.routing_key,
                envelope,
                PublishFlags(request));
        if (!published) {
            FOUNDATION_LOG_ERROR(
                "AMQP async publish command was rejected locally for exchange '"
                << request.exchange << "'");
            return;
        }

        if (publisher_confirms_ready_) {
            ++next_publish_confirm_tag_;
        }
    });
}

foundation::base::Result<PublishReceipt> RabbitMqConnectionDriver::PublishConfirmed(
    const PublishRequest& request,
    const PublishConfirmOptions& options) {
    PublishRequest effective_request = request;
    if (options.require_routable) {
        effective_request.mandatory = true;
    }

    foundation::base::Result<void> target_result =
        ValidatePublishTarget(
            effective_request.exchange,
            effective_request.routing_key);
    if (!target_result.IsOk()) {
        return foundation::base::Result<PublishReceipt>(
            target_result.GetError(),
            target_result.GetMessage());
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!thread_.joinable() || stop_requested_) {
            return foundation::base::Result<PublishReceipt>(
                foundation::base::ErrorCode::kInvalidState,
                "AMQP connection driver is not running");
        }
        if (state_ != ConnectionState::Connected) {
            return foundation::base::Result<PublishReceipt>(
                foundation::base::ErrorCode::kDisconnected,
                "AMQP connection is not ready");
        }
    }

    std::shared_ptr<PendingPublishResult> pending(new PendingPublishResult());
    foundation::base::Result<void> enqueue = EnqueueCommand(
        [this, effective_request, pending]() {
            if (!IsReadyLocked()) {
                CompletePendingPublishResult(
                    pending,
                    foundation::base::Result<PublishReceipt>(
                        foundation::base::ErrorCode::kDisconnected,
                        "AMQP connection is not ready"));
                return;
            }

            if (!publish_channel_ || !publisher_confirms_ready_) {
                CompletePendingPublishResult(
                    pending,
                    foundation::base::Result<PublishReceipt>(
                        foundation::base::ErrorCode::kNotSupported,
                        "AMQP publisher confirms are not available"));
                return;
            }

            const char* payload =
                effective_request.payload.empty()
                    ? ""
                    : &effective_request.payload[0];
            AMQP::Envelope envelope(payload, effective_request.payload.size());
            foundation::base::Result<void> envelope_result =
                FillEnvelope(effective_request, &envelope);
            if (!envelope_result.IsOk()) {
                CompletePendingPublishResult(
                    pending,
                    foundation::base::Result<PublishReceipt>(
                        envelope_result.GetError(),
                        envelope_result.GetMessage()));
                return;
            }

            const std::uint64_t delivery_tag = next_publish_confirm_tag_;
            bool published = publish_channel_->publish(
                    effective_request.exchange,
                    effective_request.routing_key,
                    envelope,
                    PublishFlags(effective_request));
            if (!published) {
                CompletePendingPublishResult(
                    pending,
                    foundation::base::Result<PublishReceipt>(
                        foundation::base::ErrorCode::kOperationFailed,
                        "AMQP publish command was rejected locally"));
                return;
            }

            PendingPublishConfirm confirm;
            confirm.request = effective_request;
            confirm.result = pending;
            pending_publish_confirms_[delivery_tag] = confirm;
            ++next_publish_confirm_tag_;
        });

    if (!enqueue.IsOk()) {
        return foundation::base::Result<PublishReceipt>(
            enqueue.GetError(),
            enqueue.GetMessage());
    }

    return WaitPendingPublishResult(pending, options.timeout_ms);
}

foundation::base::Result<void> RabbitMqConnectionDriver::DeclareExchange(
    const ExchangeSpec& spec) {
    if (spec.name.empty()) {
        return MakeInvalidArgument("Exchange name must not be empty");
    }

    std::shared_ptr<PendingResult> pending(new PendingResult());
    foundation::base::Result<void> enqueue = EnqueueCommand(
        [this, spec, pending]() {
            if (!IsReadyLocked()) {
                CompletePendingResult(
                    pending,
                    MakeDisconnected("AMQP connection is not ready"));
                return;
            }

            foundation::base::Result<AMQP::Table> arguments =
                ConfigObjectToAmqpTable(spec.arguments);
            if (!arguments.IsOk()) {
                CompletePendingResult(
                    pending,
                    foundation::base::Result<void>(
                        arguments.GetError(),
                        arguments.GetMessage()));
                return;
            }

            admin_channel_->declareExchange(
                spec.name,
                ToAmqpExchangeType(spec.type),
                ToExchangeFlags(spec),
                arguments.Value())
                .onSuccess([this, spec, pending]() {
                    UpsertExchangeSpec(spec, &config_.exchanges);
                    CompletePendingResult(
                        pending,
                        foundation::base::MakeSuccess());
                })
                .onError([pending](const char* message) {
                    CompletePendingResult(
                        pending,
                        foundation::base::Result<void>(
                            foundation::base::ErrorCode::kOperationFailed,
                            MakeErrorMessage(
                                "DeclareExchange failed",
                                message ? message : "")));
                });
        });

    if (!enqueue.IsOk()) {
        return enqueue;
    }

    return WaitPendingResult(pending);
}

foundation::base::Result<void> RabbitMqConnectionDriver::DeclareQueue(
    const QueueSpec& spec) {
    if (spec.name.empty()) {
        return MakeInvalidArgument("Queue name must not be empty");
    }

    std::shared_ptr<PendingResult> pending(new PendingResult());
    foundation::base::Result<void> enqueue = EnqueueCommand(
        [this, spec, pending]() {
            if (!IsReadyLocked()) {
                CompletePendingResult(
                    pending,
                    MakeDisconnected("AMQP connection is not ready"));
                return;
            }

            foundation::base::Result<AMQP::Table> arguments =
                ConfigObjectToAmqpTable(spec.arguments);
            if (!arguments.IsOk()) {
                CompletePendingResult(
                    pending,
                    foundation::base::Result<void>(
                        arguments.GetError(),
                        arguments.GetMessage()));
                return;
            }

            admin_channel_->declareQueue(
                spec.name,
                ToQueueFlags(spec),
                arguments.Value())
                .onSuccess([this, spec, pending](const std::string&, uint32_t, uint32_t) {
                    UpsertQueueSpec(spec, &config_.queues);
                    CompletePendingResult(
                        pending,
                        foundation::base::MakeSuccess());
                })
                .onError([pending](const char* message) {
                    CompletePendingResult(
                        pending,
                        foundation::base::Result<void>(
                            foundation::base::ErrorCode::kOperationFailed,
                            MakeErrorMessage(
                                "DeclareQueue failed",
                                message ? message : "")));
                });
        });

    if (!enqueue.IsOk()) {
        return enqueue;
    }

    return WaitPendingResult(pending);
}

foundation::base::Result<void> RabbitMqConnectionDriver::BindQueue(
    const BindingSpec& spec) {
    if (spec.exchange.empty() || spec.queue.empty()) {
        return MakeInvalidArgument(
            "BindQueue requires non-empty exchange and queue names");
    }

    std::shared_ptr<PendingResult> pending(new PendingResult());
    foundation::base::Result<void> enqueue = EnqueueCommand(
        [this, spec, pending]() {
            if (!IsReadyLocked()) {
                CompletePendingResult(
                    pending,
                    MakeDisconnected("AMQP connection is not ready"));
                return;
            }

            foundation::base::Result<AMQP::Table> arguments =
                ConfigObjectToAmqpTable(spec.arguments);
            if (!arguments.IsOk()) {
                CompletePendingResult(
                    pending,
                    foundation::base::Result<void>(
                        arguments.GetError(),
                        arguments.GetMessage()));
                return;
            }

            admin_channel_->bindQueue(
                spec.exchange,
                spec.queue,
                spec.routing_key,
                arguments.Value())
                .onSuccess([this, spec, pending]() {
                    UpsertBindingSpec(spec, &config_.bindings);
                    CompletePendingResult(
                        pending,
                        foundation::base::MakeSuccess());
                })
                .onError([pending](const char* message) {
                    CompletePendingResult(
                        pending,
                        foundation::base::Result<void>(
                            foundation::base::ErrorCode::kOperationFailed,
                            MakeErrorMessage(
                                "BindQueue failed",
                                message ? message : "")));
                });
        });

    if (!enqueue.IsOk()) {
        return enqueue;
    }

    return WaitPendingResult(pending);
}

foundation::base::Result<void> RabbitMqConnectionDriver::CompleteDelivery(
    const DeliveryContext& delivery,
    ConsumeAction action) {
    if (delivery.consumer_name.empty() || delivery.auto_ack) {
        return foundation::base::MakeSuccess();
    }

    std::shared_ptr<PendingResult> pending(new PendingResult());
    foundation::base::Result<void> enqueue = EnqueueCommand(
        [this, delivery, action, pending]() {
            std::map<std::string, ConsumerRuntime>::iterator it =
                consumer_runtimes_.find(delivery.consumer_name);
            if (it == consumer_runtimes_.end() || !it->second.channel) {
                CompletePendingResult(
                    pending,
                    MakeDisconnected(
                        "Consumer channel is unavailable for delivery completion"));
                return;
            }

            bool ok = false;
            switch (action) {
                case ConsumeAction::Ack:
                    ok = it->second.channel->ack(delivery.delivery_tag);
                    break;
                case ConsumeAction::Requeue:
                    ok = it->second.channel->reject(
                        delivery.delivery_tag,
                        AMQP::requeue);
                    break;
                case ConsumeAction::Reject:
                    ok = it->second.channel->reject(delivery.delivery_tag);
                    break;
                default:
                    break;
            }

            if (!ok) {
                CompletePendingResult(
                    pending,
                    foundation::base::Result<void>(
                        foundation::base::ErrorCode::kOperationFailed,
                        "Failed to submit ACK/NACK command to AMQP channel"));
                return;
            }

            CompletePendingResult(
                pending,
                foundation::base::MakeSuccess());
        });

    if (!enqueue.IsOk()) {
        return enqueue;
    }

    return WaitPendingResult(pending);
}

foundation::base::Result<void> RabbitMqConnectionDriver::CompleteDeliveryAsync(
    const DeliveryContext& delivery,
    ConsumeAction action) {
    if (delivery.consumer_name.empty() || delivery.auto_ack) {
        return foundation::base::MakeSuccess();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!thread_.joinable() || stop_requested_) {
            return MakeInvalidState("AMQP connection driver is not running");
        }
    }

    return EnqueueCommand([this, delivery, action]() {
        std::map<std::string, ConsumerRuntime>::iterator it =
            consumer_runtimes_.find(delivery.consumer_name);
        if (it == consumer_runtimes_.end() || !it->second.channel) {
            FOUNDATION_LOG_WARNING(
                "Skipped delivery completion because consumer channel is unavailable for '"
                << delivery.consumer_name << "'");
            return;
        }

        bool ok = false;
        switch (action) {
            case ConsumeAction::Ack:
                ok = it->second.channel->ack(delivery.delivery_tag);
                break;
            case ConsumeAction::Requeue:
                ok = it->second.channel->reject(
                    delivery.delivery_tag,
                    AMQP::requeue);
                break;
            case ConsumeAction::Reject:
                ok = it->second.channel->reject(delivery.delivery_tag);
                break;
            default:
                break;
        }

        if (!ok) {
            FOUNDATION_LOG_ERROR(
                "Failed to submit async ACK/NACK command for consumer '"
                << delivery.consumer_name << "'");
        }
    });
}

ConnectionState RabbitMqConnectionDriver::GetConnectionState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::string RabbitMqConnectionDriver::LastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

void RabbitMqConnectionDriver::onData(AMQP::Connection*,
                                      const char* data,
                                      size_t size) {
    if (data == NULL || size == 0) {
        return;
    }

    outbound_.append(data, size);
}

void RabbitMqConnectionDriver::onReady(AMQP::Connection*) {
    FOUNDATION_LOG_INFO("AMQP handshake completed");
    handshake_ready_ = true;
    EnablePublisherConfirms();
}

void RabbitMqConnectionDriver::onError(AMQP::Connection*,
                                       const char* message) {
    RequestDisconnect(MakeErrorMessage(
        "AMQP connection error",
        message ? message : ""));
}

void RabbitMqConnectionDriver::onClosed(AMQP::Connection*) {
    RequestDisconnect("AMQP connection closed");
}

std::uint16_t RabbitMqConnectionDriver::onNegotiate(AMQP::Connection*,
                                                    std::uint16_t) {
    return config_.connection.heartbeat_seconds;
}

foundation::base::Result<void> RabbitMqConnectionDriver::EnqueueCommand(
    const std::function<void()>& command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!thread_.joinable() || stop_requested_) {
        return MakeInvalidState("AMQP connection driver is not running");
    }

    commands_.push_back(command);
    return foundation::base::MakeSuccess();
}

void RabbitMqConnectionDriver::ThreadMain() {
    SocketSystemScope socket_scope;
    if (!socket_scope.IsOk()) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = ConnectionState::Error;
        last_error_ = "Failed to initialize socket subsystem";
        return;
    }

    while (!ShouldStop()) {
        DrainCommands();

        if (!connected_) {
            if (!ShouldAttemptReconnect()) {
                SleepFor(std::chrono::milliseconds(50));
                continue;
            }

            foundation::base::Result<void> connect_result = ConnectSocket();
            if (!connect_result.IsOk()) {
                ApplyReconnectBackoff(connect_result.GetMessage());
                SleepFor(std::chrono::milliseconds(100));
                continue;
            }

            continue;
        }

        FlushOutboundData();
        PumpIncomingData();
        FlushOutboundData();
        MaybeSendHeartbeat();

        if (pending_disconnect_) {
            HandleDisconnect(pending_disconnect_reason_);
            continue;
        }

        SleepFor(std::chrono::milliseconds(10));
    }

    for (int flush_attempt = 0; flush_attempt < 50; ++flush_attempt) {
        DrainCommands();
        FlushOutboundData();
        if (!connected_ || pending_disconnect_ || outbound_.empty()) {
            break;
        }
        SleepFor(std::chrono::milliseconds(10));
    }

    HandleDisconnect("AMQP driver stopped");
}

bool RabbitMqConnectionDriver::ShouldStop() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stop_requested_;
}

void RabbitMqConnectionDriver::SleepFor(std::chrono::milliseconds duration) const {
    std::this_thread::sleep_for(duration);
}

bool RabbitMqConnectionDriver::ShouldAttemptReconnect() {
    if (!config_.connection.reconnect.enabled) {
        std::lock_guard<std::mutex> lock(mutex_);
        return !stop_requested_ && state_ == ConnectionState::Connecting;
    }

    return std::chrono::steady_clock::now() >= next_reconnect_at_;
}

void RabbitMqConnectionDriver::ApplyReconnectBackoff(const std::string& error) {
    FOUNDATION_LOG_WARNING("AMQP connect attempt failed: " << error);
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_ = error;
    state_ = config_.connection.reconnect.enabled
        ? ConnectionState::Reconnecting
        : ConnectionState::Error;
    next_reconnect_at_ =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(reconnect_delay_ms_);
    reconnect_delay_ms_ = std::min(
        reconnect_delay_ms_ * 2,
        config_.connection.reconnect.max_delay_ms);
}

foundation::base::Result<void> RabbitMqConnectionDriver::ConnectSocket() {
    AMQP::Address address("amqp://guest:guest@localhost/");
    try {
        address = AMQP::Address(config_.connection.uri);
    } catch (const std::exception& ex) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidArgument,
            MakeErrorMessage("Invalid AMQP URI", ex.what()));
    }

    std::ostringstream port_stream;
    port_stream << address.port();

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* results = NULL;
    const int getaddrinfo_result = getaddrinfo(
        address.hostname().c_str(),
        port_stream.str().c_str(),
        &hints,
        &results);
    if (getaddrinfo_result != 0 || results == NULL) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kIoError,
            "getaddrinfo failed for host '" + address.hostname() + "'");
    }

    foundation::base::Result<void> result =
        foundation::base::Result<void>(
            foundation::base::ErrorCode::kIoError,
            "Unable to connect to AMQP broker at " +
                address.hostname() + ":" + port_stream.str());

    for (struct addrinfo* current = results;
         current != NULL;
         current = current->ai_next) {
        const std::string endpoint =
            address.hostname() + ":" + port_stream.str() +
            " via " + AddressFamilyName(current->ai_family);
        NativeSocket socket_fd = socket(
            current->ai_family,
            current->ai_socktype,
            current->ai_protocol);
        if (!IsSocketValid(socket_fd)) {
            const int error_code = GetLastSocketErrorCode();
            result = foundation::base::Result<void>(
                foundation::base::ErrorCode::kIoError,
                "Unable to create socket for AMQP broker at " +
                    endpoint + ": " + FormatSocketError(error_code));
            continue;
        }

        SetSocketNonBlocking(socket_fd, true);
        SetSocketNoDelay(socket_fd);

        int connect_result = connect(
            socket_fd,
            current->ai_addr,
            static_cast<int>(current->ai_addrlen));
        if (connect_result != 0) {
            const int error_code = GetLastSocketErrorCode();
            if (!ConnectInProgressError(error_code)) {
                result = foundation::base::Result<void>(
                    foundation::base::ErrorCode::kIoError,
                    "Immediate connect failure to AMQP broker at " +
                        endpoint + ": " + FormatSocketError(error_code));
                CloseSocketHandle(&socket_fd);
                continue;
            }

            fd_set write_set;
            FD_ZERO(&write_set);
            FD_SET(socket_fd, &write_set);

            struct timeval timeout;
            timeout.tv_sec = config_.connection.connect_timeout_ms / 1000;
            timeout.tv_usec =
                (config_.connection.connect_timeout_ms % 1000) * 1000;

            int select_result = select(
#if FOUNDATION_PLATFORM_WINDOWS
                0,
#else
                socket_fd + 1,
#endif
                NULL,
                &write_set,
                NULL,
                &timeout);
            if (select_result <= 0) {
                if (select_result == 0) {
                    result = foundation::base::Result<void>(
                        foundation::base::ErrorCode::kTimeout,
                        "Timed out connecting to AMQP broker at " +
                            endpoint + " after connect_timeout_ms=" +
                            std::to_string(config_.connection.connect_timeout_ms));
                } else {
                    const int select_error = GetLastSocketErrorCode();
                    result = foundation::base::Result<void>(
                        foundation::base::ErrorCode::kIoError,
                        "Connect select failed for AMQP broker at " +
                            endpoint + ": " + FormatSocketError(select_error));
                }
                CloseSocketHandle(&socket_fd);
                continue;
            }

            int socket_error = 0;
            SocketOptionLength option_length =
                static_cast<SocketOptionLength>(sizeof(socket_error));
            if (getsockopt(
                    socket_fd,
                    SOL_SOCKET,
                    SO_ERROR,
                    reinterpret_cast<char*>(&socket_error),
                    &option_length) != 0) {
                const int getsockopt_error = GetLastSocketErrorCode();
                result = foundation::base::Result<void>(
                    foundation::base::ErrorCode::kIoError,
                    "Failed to read connect status for AMQP broker at " +
                        endpoint + ": " + FormatSocketError(getsockopt_error));
                CloseSocketHandle(&socket_fd);
                continue;
            }
            if (socket_error != 0) {
                result = foundation::base::Result<void>(
                    foundation::base::ErrorCode::kIoError,
                    "Connect failed for AMQP broker at " +
                        endpoint + ": " + FormatSocketError(socket_error));
                CloseSocketHandle(&socket_fd);
                continue;
            }
        }

        socket_fd_ = socket_fd;
        connected_ = true;
        ready_ = false;
        handshake_ready_ = false;
        publisher_confirms_ready_ = false;
        next_publish_confirm_tag_ = 1;
        pending_publish_confirms_.clear();
        pending_disconnect_ = false;
        pending_disconnect_reason_.clear();
        outbound_.clear();
        inbound_.clear();
        reconnect_delay_ms_ = config_.connection.reconnect.initial_delay_ms;
        last_heartbeat_sent_ = std::chrono::steady_clock::now();

        connection_.reset(new AMQP::Connection(
            this,
            address.login(),
            address.vhost()));
        admin_channel_.reset(new AMQP::Channel(connection_.get()));
        admin_channel_->onError([this](const char* message) {
            RequestDisconnect(MakeErrorMessage(
                "AMQP admin channel error",
                message ? message : ""));
        });
        publish_channel_.reset(new AMQP::Channel(connection_.get()));
        publish_channel_->onError([this](const char* message) {
            RequestDisconnect(MakeErrorMessage(
                "AMQP publisher channel error",
                message ? message : ""));
        });
        publish_channel_->recall().onReceived(
            [this](const AMQP::Message& message,
                   int16_t code,
                   const std::string& description) {
                HandleReturnedMessage(message, code, description);
            });

        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = ConnectionState::Connecting;
            last_error_.clear();
        }

        result = foundation::base::MakeSuccess();
        break;
    }

    freeaddrinfo(results);
    return result;
}

void RabbitMqConnectionDriver::FlushOutboundData() {
    while (connected_ && !outbound_.empty()) {
        int sent = send(
            socket_fd_,
            outbound_.data(),
            static_cast<int>(outbound_.size()),
            0);
        if (sent > 0) {
            outbound_.erase(0, static_cast<std::size_t>(sent));
            continue;
        }

        const int error_code = GetLastSocketErrorCode();
        if (WouldBlockError(error_code)) {
            return;
        }

        RequestDisconnect(MakeErrorMessage(
            "AMQP socket send failed",
            GetLastSocketErrorMessage(error_code)));
        return;
    }
}

void RabbitMqConnectionDriver::PumpIncomingData() {
    if (!connected_) {
        return;
    }

    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(socket_fd_, &read_set);

    struct timeval timeout;
    timeout.tv_sec = config_.connection.socket_timeout_ms / 1000;
    timeout.tv_usec =
        (config_.connection.socket_timeout_ms % 1000) * 1000;

    const int select_result = select(
#if FOUNDATION_PLATFORM_WINDOWS
        0,
#else
        socket_fd_ + 1,
#endif
        &read_set,
        NULL,
        NULL,
        &timeout);
    if (select_result < 0) {
        RequestDisconnect("AMQP socket select failed");
        return;
    }
    if (select_result == 0 || !FD_ISSET(socket_fd_, &read_set)) {
        return;
    }

    char buffer[8192];
    int received = recv(socket_fd_, buffer, sizeof(buffer), 0);
    if (received == 0) {
        RequestDisconnect("AMQP broker closed the socket");
        return;
    }
    if (received < 0) {
        const int error_code = GetLastSocketErrorCode();
        if (WouldBlockError(error_code)) {
            return;
        }
        RequestDisconnect(MakeErrorMessage(
            "AMQP socket receive failed",
            GetLastSocketErrorMessage(error_code)));
        return;
    }

    inbound_.insert(inbound_.end(), buffer, buffer + received);
    while (!inbound_.empty() && connection_) {
        const std::uint64_t parsed = connection_->parse(
            &inbound_[0],
            inbound_.size());
        if (parsed == 0) {
            break;
        }
        inbound_.erase(
            inbound_.begin(),
            inbound_.begin() + static_cast<std::ptrdiff_t>(parsed));
    }
}

void RabbitMqConnectionDriver::MaybeSendHeartbeat() {
    if (!connected_ || !connection_ || !handshake_ready_ ||
        config_.connection.heartbeat_seconds == 0) {
        return;
    }

    const std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now();
    const std::chrono::milliseconds interval(
        std::max<int>(1, config_.connection.heartbeat_seconds * 500));
    if (last_heartbeat_sent_ != std::chrono::steady_clock::time_point::min() &&
        now - last_heartbeat_sent_ < interval) {
        return;
    }

    connection_->heartbeat();
    last_heartbeat_sent_ = now;
}

void RabbitMqConnectionDriver::DrainCommands() {
    std::deque<std::function<void()> > local_commands;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        local_commands.swap(commands_);
    }

    while (!local_commands.empty()) {
        std::function<void()> command = local_commands.front();
        local_commands.pop_front();
        command();
        FlushOutboundData();
    }
}

bool RabbitMqConnectionDriver::IsReadyLocked() const {
    return connected_ && ready_ && admin_channel_.get() != NULL &&
           publish_channel_.get() != NULL &&
           (!config_.publisher_confirms_enabled || publisher_confirms_ready_);
}

void RabbitMqConnectionDriver::RequestDisconnect(const std::string& reason) {
    FOUNDATION_LOG_WARNING("AMQP disconnect requested: " << reason);
    pending_disconnect_ = true;
    pending_disconnect_reason_ = reason;
}

void RabbitMqConnectionDriver::HandleDisconnect(const std::string& reason) {
    FailPendingPublishes(reason);

    if (connection_) {
        connection_->fail(reason.c_str());
    }

    consumer_runtimes_.clear();
    publish_channel_.reset();
    admin_channel_.reset();
    connection_.reset();
    CloseSocketHandle(&socket_fd_);
    connected_ = false;
    ready_ = false;
    handshake_ready_ = false;
    publisher_confirms_ready_ = false;
    next_publish_confirm_tag_ = 1;
    pending_disconnect_ = false;
    outbound_.clear();
    inbound_.clear();

    std::lock_guard<std::mutex> lock(mutex_);
    if (!stop_requested_) {
        last_error_ = reason;
        state_ = config_.connection.reconnect.enabled
            ? ConnectionState::Reconnecting
            : ConnectionState::Error;
        next_reconnect_at_ =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(reconnect_delay_ms_);
        reconnect_delay_ms_ = std::min(
            reconnect_delay_ms_ * 2,
            config_.connection.reconnect.max_delay_ms);
    } else {
        state_ = ConnectionState::Stopped;
    }
}

void RabbitMqConnectionDriver::StartBootstrap() {
    FOUNDATION_LOG_INFO("AMQP connection ready, rebuilding topology");
    BootstrapExchanges(0);
}

void RabbitMqConnectionDriver::EnablePublisherConfirms() {
    if (!publish_channel_) {
        RequestDisconnect("AMQP publisher channel is unavailable");
        return;
    }

    if (!config_.publisher_confirms_enabled) {
        FOUNDATION_LOG_INFO("AMQP publisher confirms disabled by config");
        StartBootstrap();
        return;
    }

    AMQP::DeferredConfirm& confirm = publish_channel_->confirmSelect();
    confirm.onSuccess([this]() {
        publisher_confirms_ready_ = true;
        next_publish_confirm_tag_ = 1;
        FOUNDATION_LOG_INFO("AMQP publisher confirms enabled");
        StartBootstrap();
    });
    confirm.onAck([this](std::uint64_t delivery_tag, bool multiple) {
        CompletePublishConfirm(
            delivery_tag,
            multiple,
            PublishDisposition::BrokerAccepted,
            0,
            "");
    });
    confirm.onNack([this](
            std::uint64_t delivery_tag,
            bool multiple,
            bool) {
        CompletePublishConfirm(
            delivery_tag,
            multiple,
            PublishDisposition::BrokerRejected,
            0,
            "Broker sent negative publisher acknowledgement");
    });
    confirm.onError([this](const char* message) {
        RequestDisconnect(MakeErrorMessage(
            "AMQP publisher confirms are not available",
            message ? message : ""));
    });
}

void RabbitMqConnectionDriver::BootstrapExchanges(std::size_t index) {
    if (index >= config_.exchanges.size()) {
        BootstrapQueues(0);
        return;
    }

    const ExchangeSpec spec = config_.exchanges[index];
    FOUNDATION_LOG_INFO(
        "AMQP bootstrap declare exchange '" << spec.name
        << "', passive=" << (spec.passive ? "true" : "false"));
    foundation::base::Result<AMQP::Table> arguments =
        ConfigObjectToAmqpTable(spec.arguments);
    if (!arguments.IsOk()) {
        RequestDisconnect(arguments.GetMessage());
        return;
    }

    admin_channel_->declareExchange(
        spec.name,
        ToAmqpExchangeType(spec.type),
        ToExchangeFlags(spec),
        arguments.Value())
        .onSuccess([this, spec, index]() {
            FOUNDATION_LOG_INFO(
                "AMQP bootstrap declared exchange '" << spec.name << "'");
            BootstrapExchanges(index + 1);
        })
        .onError([this, spec](const char* message) {
            RequestDisconnect(MakeErrorMessage(
                "Failed to declare exchange '" + spec.name + "'",
                message ? message : ""));
        });
}

void RabbitMqConnectionDriver::BootstrapQueues(std::size_t index) {
    if (index >= config_.queues.size()) {
        BootstrapBindings(0);
        return;
    }

    const QueueSpec spec = config_.queues[index];
    FOUNDATION_LOG_INFO(
        "AMQP bootstrap declare queue '" << spec.name
        << "', passive=" << (spec.passive ? "true" : "false")
        << ", exclusive=" << (spec.exclusive ? "true" : "false")
        << ", auto_delete=" << (spec.auto_delete ? "true" : "false"));
    foundation::base::Result<AMQP::Table> arguments =
        ConfigObjectToAmqpTable(spec.arguments);
    if (!arguments.IsOk()) {
        RequestDisconnect(arguments.GetMessage());
        return;
    }

    admin_channel_->declareQueue(
        spec.name,
        ToQueueFlags(spec),
        arguments.Value())
        .onSuccess([this, spec, index](const std::string&, uint32_t, uint32_t) {
            FOUNDATION_LOG_INFO(
                "AMQP bootstrap declared queue '" << spec.name << "'");
            BootstrapQueues(index + 1);
        })
        .onError([this, spec](const char* message) {
            RequestDisconnect(MakeErrorMessage(
                "Failed to declare queue '" + spec.name + "'",
                message ? message : ""));
        });
}

void RabbitMqConnectionDriver::BootstrapBindings(std::size_t index) {
    if (index >= config_.bindings.size()) {
        BootstrapConsumers(0);
        return;
    }

    const BindingSpec spec = config_.bindings[index];
    FOUNDATION_LOG_INFO(
        "AMQP bootstrap bind queue '" << spec.queue
        << "' to exchange '" << spec.exchange
        << "' routing_key='" << spec.routing_key << "'");
    foundation::base::Result<AMQP::Table> arguments =
        ConfigObjectToAmqpTable(spec.arguments);
    if (!arguments.IsOk()) {
        RequestDisconnect(arguments.GetMessage());
        return;
    }

    admin_channel_->bindQueue(
        spec.exchange,
        spec.queue,
        spec.routing_key,
        arguments.Value())
        .onSuccess([this, spec, index]() {
            FOUNDATION_LOG_INFO(
                "AMQP bootstrap bound queue '" << spec.queue
                << "' to exchange '" << spec.exchange << "'");
            BootstrapBindings(index + 1);
        })
        .onError([this, spec](const char* message) {
            RequestDisconnect(MakeErrorMessage(
                "Failed to bind queue '" + spec.queue + "'",
                message ? message : ""));
        });
}

void RabbitMqConnectionDriver::BootstrapConsumers(std::size_t index) {
    if (index >= config_.consumers.size()) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = ConnectionState::Connected;
        ready_ = true;
        FOUNDATION_LOG_INFO("AMQP topology rebuild completed");
        return;
    }

    const ConsumerSpec spec = config_.consumers[index];
    FOUNDATION_LOG_INFO(
        "AMQP bootstrap start consumer '" << spec.name
        << "' queue='" << spec.queue
        << "' prefetch=" << spec.prefetch_count);
    ConsumerRuntime& runtime = consumer_runtimes_[spec.name];
    runtime.spec = spec;
    runtime.channel.reset(new AMQP::Channel(connection_.get()));
    runtime.active = false;
    runtime.active_tag.clear();
    runtime.channel->onError([this, spec](const char* message) {
        RequestDisconnect(MakeErrorMessage(
            "Consumer channel error for '" + spec.name + "'",
            message ? message : ""));
    });

    if (spec.prefetch_count > 0) {
        runtime.channel->setQos(spec.prefetch_count)
            .onSuccess([this, spec, index]() {
                FOUNDATION_LOG_INFO(
                    "AMQP bootstrap applied qos for consumer '"
                    << spec.name << "'");
                StartConsumer(index);
            })
            .onError([this, spec](const char* message) {
                RequestDisconnect(MakeErrorMessage(
                    "Failed to apply qos for consumer '" + spec.name + "'",
                    message ? message : ""));
            });
    } else {
        StartConsumer(index);
    }
}

void RabbitMqConnectionDriver::StartConsumer(std::size_t index) {
    const ConsumerSpec spec = config_.consumers[index];
    std::map<std::string, ConsumerRuntime>::iterator runtime_it =
        consumer_runtimes_.find(spec.name);
    if (runtime_it == consumer_runtimes_.end() || !runtime_it->second.channel) {
        RequestDisconnect(
            "Consumer runtime missing when starting consumer '" + spec.name + "'");
        return;
    }

    foundation::base::Result<AMQP::Table> arguments =
        ConfigObjectToAmqpTable(spec.arguments);
    if (!arguments.IsOk()) {
        RequestDisconnect(arguments.GetMessage());
        return;
    }

    runtime_it->second.channel->consume(
        spec.queue,
        spec.consumer_tag,
        ToConsumeFlags(spec),
        arguments.Value())
        .onSuccess([this, spec, index](const std::string& tag) {
            std::map<std::string, ConsumerRuntime>::iterator runtime =
                consumer_runtimes_.find(spec.name);
            if (runtime != consumer_runtimes_.end()) {
                runtime->second.active_tag = tag;
                runtime->second.active = true;
            }
            FOUNDATION_LOG_INFO(
                "AMQP bootstrap consumer '" << spec.name
                << "' started with tag '" << tag << "'");
            BootstrapConsumers(index + 1);
        })
        .onReceived([this, spec](
            const AMQP::Message& message,
            std::uint64_t delivery_tag,
            bool redelivered) {
            HandleIncomingMessage(spec, message, delivery_tag, redelivered);
        })
        .onCancelled([this, spec](const std::string&) {
            RequestDisconnect(
                "AMQP consumer '" + spec.name + "' was cancelled by broker");
        })
        .onError([this, spec](const char* message) {
            RequestDisconnect(MakeErrorMessage(
                "Failed to start consumer '" + spec.name + "'",
                message ? message : ""));
        });
}

void RabbitMqConnectionDriver::CompletePublishConfirm(
    std::uint64_t delivery_tag,
    bool multiple,
    PublishDisposition disposition,
    int reply_code,
    const std::string& reply_text) {
    if (pending_publish_confirms_.empty()) {
        return;
    }

    std::vector<std::uint64_t> completed_tags;
    if (multiple) {
        for (std::map<std::uint64_t, PendingPublishConfirm>::const_iterator it =
                 pending_publish_confirms_.begin();
             it != pending_publish_confirms_.end() && it->first <= delivery_tag;
             ++it) {
            completed_tags.push_back(it->first);
        }
    } else if (pending_publish_confirms_.find(delivery_tag) !=
               pending_publish_confirms_.end()) {
        completed_tags.push_back(delivery_tag);
    }

    for (std::size_t index = 0; index < completed_tags.size(); ++index) {
        std::map<std::uint64_t, PendingPublishConfirm>::iterator it =
            pending_publish_confirms_.find(completed_tags[index]);
        if (it == pending_publish_confirms_.end()) {
            continue;
        }

        PublishReceipt receipt;
        if (it->second.returned) {
            receipt.disposition = PublishDisposition::Returned;
            receipt.reply_code = it->second.reply_code;
            receipt.reply_text = it->second.reply_text;
        } else {
            receipt.disposition = disposition;
            receipt.reply_code = reply_code;
            receipt.reply_text = reply_text;
        }

        CompletePendingPublishResult(
            it->second.result,
            foundation::base::Result<PublishReceipt>(receipt));
        pending_publish_confirms_.erase(it);
    }
}

void RabbitMqConnectionDriver::FailPendingPublishes(const std::string& reason) {
    if (pending_publish_confirms_.empty()) {
        return;
    }

    const std::string message = reason.empty()
        ? std::string("AMQP connection closed before publisher confirm")
        : reason;
    std::map<std::uint64_t, PendingPublishConfirm> pending;
    pending.swap(pending_publish_confirms_);

    for (std::map<std::uint64_t, PendingPublishConfirm>::iterator it =
             pending.begin();
         it != pending.end();
         ++it) {
        CompletePendingPublishResult(
            it->second.result,
            foundation::base::Result<PublishReceipt>(
                foundation::base::ErrorCode::kDisconnected,
                message));
    }
}

bool RabbitMqConnectionDriver::MarkReturnedPublish(
    const AMQP::Message& message,
    int16_t code,
    const std::string& description) {
    for (std::map<std::uint64_t, PendingPublishConfirm>::iterator it =
             pending_publish_confirms_.begin();
         it != pending_publish_confirms_.end();
         ++it) {
        PendingPublishConfirm& pending = it->second;
        if (pending.returned || !pending.request.mandatory) {
            continue;
        }
        if (pending.request.exchange != message.exchange() ||
            pending.request.routing_key != message.routingkey()) {
            continue;
        }
        if (pending.request.payload.size() != message.bodySize()) {
            continue;
        }
        if (!pending.request.payload.empty() &&
            std::memcmp(
                &pending.request.payload[0],
                message.body(),
                pending.request.payload.size()) != 0) {
            continue;
        }
        if (!pending.request.content_type.empty() &&
            pending.request.content_type != message.contentType()) {
            continue;
        }
        if (!pending.request.correlation_id.empty() &&
            pending.request.correlation_id != message.correlationID()) {
            continue;
        }
        if (!pending.request.reply_to.empty() &&
            pending.request.reply_to != message.replyTo()) {
            continue;
        }

        pending.returned = true;
        pending.reply_code = static_cast<int>(code);
        pending.reply_text = description;
        return true;
    }

    return false;
}

void RabbitMqConnectionDriver::HandleReturnedMessage(
    const AMQP::Message& message,
    int16_t code,
    const std::string& description) {
    if (MarkReturnedPublish(message, code, description)) {
        return;
    }

    const std::string correlation_id = message.correlationID();
    FOUNDATION_LOG_WARNING(
        "AMQP broker returned mandatory publish"
        << (correlation_id.empty()
                ? std::string("")
                : std::string(" correlation_id='") + correlation_id + "'")
        << ", code=" << code << ", description=" << description);
}

void RabbitMqConnectionDriver::HandleIncomingMessage(
    const ConsumerSpec& spec,
    const AMQP::Message& message,
    std::uint64_t delivery_tag,
    bool redelivered) {
    foundation::base::Result<foundation::config::ConfigValue::Object> headers =
        AmqpTableToConfigObject(message.headers());
    if (!headers.IsOk()) {
        FOUNDATION_LOG_ERROR(
            "Failed to convert AMQP headers for consumer '" << spec.name
            << "': " << headers.GetMessage());
        if (!spec.auto_ack) {
            DeliveryContext delivery;
            delivery.consumer_name = spec.name;
            delivery.delivery_tag = delivery_tag;
            delivery.auto_ack = spec.auto_ack;
            LogResultIfError(
                CompleteDeliveryAsync(delivery, ConsumeAction::Reject),
                "Reject message after header conversion failure");
        }
        return;
    }

    IncomingMessage incoming;
    incoming.consumer_name = spec.name;
    incoming.exchange = message.exchange();
    incoming.routing_key = message.routingkey();
    incoming.payload.assign(
        message.body(),
        message.body() + static_cast<std::ptrdiff_t>(message.bodySize()));
    incoming.content_type = message.contentType();
    incoming.correlation_id = message.correlationID();
    incoming.reply_to = message.replyTo();
    incoming.headers = headers.Value();
    incoming.redelivered = redelivered;

    DeliveryContext delivery;
    delivery.consumer_name = spec.name;
    delivery.delivery_tag = delivery_tag;
    delivery.auto_ack = spec.auto_ack;

    if (delivery_callback_) {
        delivery_callback_(incoming, delivery);
    }
}

foundation::base::Result<void> PublishWithDriver(
    const std::shared_ptr<RabbitMqConnectionDriver>& driver,
    const PublishRequest& request) {
    if (!driver) {
        return MakeDisconnected("AMQP bus module is not started");
    }

    return driver->Publish(request);
}

foundation::base::Result<void> PublishAsyncWithDriver(
    const std::shared_ptr<RabbitMqConnectionDriver>& driver,
    const PublishRequest& request) {
    if (!driver) {
        return MakeDisconnected("AMQP bus module is not started");
    }

    return driver->PublishAsync(request);
}

foundation::base::Result<PublishReceipt> PublishConfirmedWithDriver(
    const std::shared_ptr<RabbitMqConnectionDriver>& driver,
    const PublishRequest& request,
    const PublishConfirmOptions& options) {
    if (!driver) {
        return foundation::base::Result<PublishReceipt>(
            foundation::base::ErrorCode::kDisconnected,
            "AMQP bus module is not started");
    }

    return driver->PublishConfirmed(request, options);
}

foundation::base::Result<void> DeclareExchangeWithDriver(
    const std::shared_ptr<RabbitMqConnectionDriver>& driver,
    const ExchangeSpec& spec) {
    if (!driver) {
        return MakeDisconnected("AMQP bus module is not started");
    }

    return driver->DeclareExchange(spec);
}

foundation::base::Result<void> DeclareQueueWithDriver(
    const std::shared_ptr<RabbitMqConnectionDriver>& driver,
    const QueueSpec& spec) {
    if (!driver) {
        return MakeDisconnected("AMQP bus module is not started");
    }

    return driver->DeclareQueue(spec);
}

foundation::base::Result<void> BindQueueWithDriver(
    const std::shared_ptr<RabbitMqConnectionDriver>& driver,
    const BindingSpec& spec) {
    if (!driver) {
        return MakeDisconnected("AMQP bus module is not started");
    }

    return driver->BindQueue(spec);
}

ConnectionState GetConnectionStateFromDriver(
    const std::shared_ptr<RabbitMqConnectionDriver>& driver) {
    if (!driver) {
        return ConnectionState::Created;
    }

    return driver->GetConnectionState();
}

bool SupportsFeatureFromDriver(
    const std::shared_ptr<RabbitMqConnectionDriver>&,
    MessageBusFeature feature) {
    switch (feature) {
        case MessageBusFeature::PublisherConfirm:
        case MessageBusFeature::MandatoryReturn:
            return true;
        default:
            return false;
    }
}

RabbitMqBusModule::RabbitMqBusModule()
    : shared_state_(new RabbitMqBusSharedState()),
      service_proxy_(new MessageBusServiceProxy(shared_state_)) {
}

RabbitMqBusModule::~RabbitMqBusModule() {
}

std::string RabbitMqBusModule::ModuleType() const {
    return "amqp_bus";
}

std::vector<std::string> RabbitMqBusModule::ModuleTypeAliases() const {
    std::vector<std::string> aliases;
    aliases.push_back("rabbitmq_bus");
    return aliases;
}

std::string RabbitMqBusModule::ModuleVersion() const {
    return "1.0.0";
}

foundation::base::Result<void> RabbitMqBusModule::Publish(
    const PublishRequest& request) {
    return service_proxy_->Publish(request);
}

foundation::base::Result<void> RabbitMqBusModule::PublishAsync(
    const PublishRequest& request) {
    return service_proxy_->PublishAsync(request);
}

foundation::base::Result<PublishReceipt> RabbitMqBusModule::PublishConfirmed(
    const PublishRequest& request,
    const PublishConfirmOptions& options) {
    return service_proxy_->PublishConfirmed(request, options);
}

foundation::base::Result<void> RabbitMqBusModule::RegisterConsumerHandler(
    const std::string& consumer_name,
    MessageHandler handler) {
    return service_proxy_->RegisterConsumerHandler(consumer_name, handler);
}

foundation::base::Result<void> RabbitMqBusModule::UnregisterConsumerHandler(
    const std::string& consumer_name) {
    return service_proxy_->UnregisterConsumerHandler(consumer_name);
}

foundation::base::Result<void> RabbitMqBusModule::DeclareExchange(
    const ExchangeSpec& spec) {
    return service_proxy_->DeclareExchange(spec);
}

foundation::base::Result<void> RabbitMqBusModule::DeclareQueue(
    const QueueSpec& spec) {
    return service_proxy_->DeclareQueue(spec);
}

foundation::base::Result<void> RabbitMqBusModule::BindQueue(
    const BindingSpec& spec) {
    return service_proxy_->BindQueue(spec);
}

ConnectionState RabbitMqBusModule::GetConnectionState() const {
    return service_proxy_->GetConnectionState();
}

bool RabbitMqBusModule::SupportsFeature(MessageBusFeature feature) const {
    return service_proxy_->SupportsFeature(feature);
}

foundation::base::Result<void> RabbitMqBusModule::OnInit() {
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

    foundation::base::Result<RabbitMqBusConfig> parsed =
        ParseBusConfig(config.Value());
    if (!parsed.IsOk()) {
        return foundation::base::Result<void>(
            parsed.GetError(),
            parsed.GetMessage());
    }

    std::shared_ptr<RabbitMqBusConfig> parsed_config(
        new RabbitMqBusConfig(parsed.Value()));
    std::shared_ptr<foundation::concurrent::ThreadPool> worker_pool(
        new foundation::concurrent::ThreadPool(parsed_config->worker_thread_count));
    {
        std::lock_guard<std::mutex> lock(shared_state_->mutex);
        shared_state_->config = parsed_config;
        shared_state_->handlers.clear();
        shared_state_->worker_pool = worker_pool;
        shared_state_->driver.reset();
        shared_state_->stopping = false;
    }

    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> RabbitMqBusModule::OnStart() {
    std::shared_ptr<RabbitMqBusConfig> config;
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

    std::shared_ptr<RabbitMqBusSharedState> state = shared_state_;
    std::shared_ptr<RabbitMqConnectionDriver> driver(
        new RabbitMqConnectionDriver(
            *config,
            [state](const IncomingMessage& incoming,
                    const DeliveryContext& delivery) {
                MessageHandler handler;
                std::shared_ptr<RabbitMqConnectionDriver> driver_ref;
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
                            driver_ref->CompleteDeliveryAsync(
                                delivery,
                                ConsumeAction::Requeue),
                            "Requeue message because AMQP bus is stopping");
                    }
                    return;
                }

                if (!worker_pool_ref) {
                    if (!delivery.auto_ack && driver_ref) {
                        LogResultIfError(
                            driver_ref->CompleteDeliveryAsync(
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
                                    driver_ref->CompleteDeliveryAsync(delivery, action),
                                    "Failed to complete delivery for consumer '" +
                                        incoming.consumer_name + "'");
                            }
                        });

                if (!submitted.IsOk() && !delivery.auto_ack && driver_ref) {
                    LogResultIfError(
                        driver_ref->CompleteDeliveryAsync(
                            delivery,
                            ConsumeAction::Requeue),
                        "Requeue message because worker task submission failed");
                }
            }));

    {
        std::lock_guard<std::mutex> lock(shared_state_->mutex);
        shared_state_->driver = driver;
    }

    foundation::base::Result<void> start_result = driver->Start();
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

foundation::base::Result<void> RabbitMqBusModule::OnStop() {
    foundation::base::Result<void> first_error = foundation::base::MakeSuccess();
    std::shared_ptr<RabbitMqConnectionDriver> driver;
    std::shared_ptr<foundation::concurrent::ThreadPool> worker_pool;
    {
        std::lock_guard<std::mutex> lock(shared_state_->mutex);
        shared_state_->stopping = true;
        driver = shared_state_->driver;
        worker_pool = shared_state_->worker_pool;
    }

    // 先等待已提交的业务 handler 结束，再停止 AMQP driver。这样 handler 返回后
    // 仍有机会通过 driver 发送 ACK/NACK，降低停止过程中的重复投递风险。
    if (worker_pool) {
        foundation::base::Result<void> pool_result = worker_pool->Shutdown();
        if (first_error.IsOk() && !pool_result.IsOk()) {
            first_error = pool_result;
        }
    }

    if (driver) {
        foundation::base::Result<void> stop_result = driver->Stop();
        if (first_error.IsOk() && !stop_result.IsOk()) {
            first_error = stop_result;
        }
    }

    {
        std::lock_guard<std::mutex> lock(shared_state_->mutex);
        shared_state_->handlers.clear();
    }

    return first_error;
}

foundation::base::Result<void> RabbitMqBusModule::OnFini() {
    std::lock_guard<std::mutex> lock(shared_state_->mutex);
    shared_state_->stopping = false;
    shared_state_->handlers.clear();
    shared_state_->driver.reset();
    shared_state_->worker_pool.reset();
    shared_state_->config.reset(new RabbitMqBusConfig());
    return foundation::base::MakeSuccess();
}

MC_DECLARE_MODULE_FACTORY(RabbitMqBusModule)

}  // namespace messaging
}  // namespace module_context
