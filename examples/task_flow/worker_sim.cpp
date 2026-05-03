#include "common.h"

#include "foundation/base/ErrorCode.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "iplugin_tgv_etching.h"

namespace {

using foundation::base::ErrorCode;
using foundation::base::Result;
using module_context::examples::task_flow::EnsureDirectory;
using module_context::examples::task_flow::MessageBusHost;
using module_context::examples::task_flow::ParseArguments;
using module_context::examples::task_flow::ParseOptionalInt;
using module_context::examples::task_flow::ParseTaskMessage;
using module_context::examples::task_flow::RequireArgument;
using module_context::examples::task_flow::ResultMessage;
using module_context::examples::task_flow::SerializeResultMessage;
using module_context::examples::task_flow::TaskMessage;
using module_context::examples::task_flow::WaitForConnected;
using module_context::examples::task_flow::kResultKindData;
using module_context::examples::task_flow::kTaskKindShutdown;
using module_context::examples::task_flow::kTaskKindTask;
using module_context::http::HttpDownloadRequest;
using module_context::messaging::ConsumeAction;
using module_context::messaging::IncomingMessage;
using module_context::messaging::PublishRequest;

const char kResultExchange[] = "mc.result.exchange";
const char kResultRoutingKey[] = "result.ready";
const char kImageIdHeader[] = "X-TaskFlow-Image-Id";
const char kImageTokenHeader[] = "X-TaskFlow-Image-Token";
const char kTaskConsumerName[] = "task_consumer";
const char kControlConsumerName[] = "control_consumer";

typedef std::chrono::steady_clock SteadyClock;

double ElapsedMillis(
    const SteadyClock::time_point& end_time,
    const SteadyClock::time_point& begin_time) {
    return static_cast<double>(
               std::chrono::duration_cast<std::chrono::microseconds>(
                   end_time - begin_time)
                   .count()) /
           1000.0;
}

std::mutex& GetLogMutex() {
    static std::mutex mutex;
    return mutex;
}

void LogInfoLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(GetLogMutex());
    std::cout << line << std::endl;
}

void LogErrorLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(GetLogMutex());
    std::cerr << line << std::endl;
}

std::string GetOptionalString(
    const std::map<std::string, std::string>& args,
    const std::string& key,
    const std::string& fallback) {
    std::map<std::string, std::string>::const_iterator it = args.find(key);
    if (it == args.end() || it->second.empty()) {
        return fallback;
    }
    return it->second;
}

bool ParseOptionalBool(
    const std::map<std::string, std::string>& args,
    const std::string& key,
    bool fallback_value) {
    std::map<std::string, std::string>::const_iterator it = args.find(key);
    if (it == args.end() || it->second.empty()) {
        return fallback_value;
    }

    std::string value = it->second;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] >= 'A' && value[index] <= 'Z') {
            value[index] = static_cast<char>(value[index] - 'A' + 'a');
        }
    }

    return value == "1" || value == "true" || value == "yes" || value == "on";
}

std::string SanitizeFileStem(const std::string& value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_') {
            sanitized.push_back(ch);
        } else {
            sanitized.push_back('_');
        }
    }
    return sanitized.empty() ? "image" : sanitized;
}

struct WorkerConfig {
    std::string module_config_path;
    std::string worker_id;
    std::string worker_dir;
    std::string http_endpoint;
    std::string http_route;
    int worker_task_threads;
    int algorithm_delay_ms;
    std::string algorithm_plugin_name;
    bool algorithm_persist_input_images;
    std::string algorithm_input_dir;
    int timeout_ms;
};

struct WorkerState {
    WorkerState()
        : shutdown_received(false),
          active_jobs(0),
          processed_count(0),
          failed_count(0),
          fatal_error(false),
          mutex(),
          state_cv() {
    }

    bool shutdown_received;
    int active_jobs;
    int processed_count;
    int failed_count;
    bool fatal_error;
    std::mutex mutex;
    std::condition_variable state_cv;
};

struct TaskOutcome {
    ConsumeAction action;
    bool counted_as_processed;

    TaskOutcome()
        : action(ConsumeAction::Requeue),
          counted_as_processed(false) {
    }
};

struct FetchImageResult {
    std::vector<char> image;
    module_context::http::HttpResponseInfo http_info;

    FetchImageResult()
        : image(),
          http_info() {
    }
};

bool IsRecoverableFetchError(ErrorCode code) {
    return code == ErrorCode::kTimeout ||
           code == ErrorCode::kDisconnected ||
           code == ErrorCode::kIoError ||
           code == ErrorCode::kBusy ||
           code == ErrorCode::kOperationFailed;
}

Result<void> PublishResultMessage(
    module_context::messaging::IMessageBusService* bus,
    const ResultMessage& result_message) {
    if (bus == NULL) {
        return Result<void>(ErrorCode::kInvalidArgument, "IMessageBusService is null");
    }

    const std::string payload = SerializeResultMessage(result_message);
    PublishRequest publish_request;
    publish_request.exchange = kResultExchange;
    publish_request.routing_key = kResultRoutingKey;
    publish_request.properties.content_type = "text/plain";
    publish_request.properties.correlation_id = result_message.image_id;
    publish_request.properties.persistent = false;
    publish_request.mandatory = false;
    publish_request.payload.assign(payload.begin(), payload.end());
    return bus->PublishAsync(publish_request);
}

Result<FetchImageResult> FetchImage(
    module_context::http::IHttpTransferService* http,
    const WorkerConfig& config,
    const TaskMessage& task_message) {
    if (http == NULL) {
        return Result<FetchImageResult>(
            ErrorCode::kInvalidArgument,
            "IHttpTransferService is null");
    }

    HttpDownloadRequest request;
    request.endpoint = config.http_endpoint;
    request.route = config.http_route;
    request.timeout_ms = config.timeout_ms == 0 ? 30000 : config.timeout_ms;
    request.max_bytes = task_message.image_bytes == 0
                            ? 0
                            : task_message.image_bytes + 1;
    request.read_buffer_bytes = task_message.http_read_buffer_bytes;
    request.write_buffer_bytes = task_message.http_write_buffer_bytes;
    request.socket_receive_buffer_bytes =
        task_message.http_socket_receive_buffer_bytes;
    request.socket_send_buffer_bytes =
        task_message.http_socket_send_buffer_bytes;
    request.headers[kImageIdHeader] = task_message.image_id;
    request.headers[kImageTokenHeader] = task_message.token;

    std::vector<char> image;
    std::size_t received_bytes = 0;
    if (task_message.image_bytes > 0) {
        image.resize(task_message.image_bytes);
    }
    Result<module_context::http::HttpResponseInfo> response = http->Download(
        request,
        [&image, &received_bytes, &task_message](
            const char* data,
            std::size_t size) -> Result<void> {
            if (data != NULL && size > 0) {
                if (task_message.image_bytes > 0) {
                    if (received_bytes + size > image.size()) {
                        return Result<void>(
                            ErrorCode::kOutOfRange,
                            "HTTP download exceeded expected image size");
                    }
                    std::memcpy(&image[received_bytes], data, size);
                    received_bytes += size;
                } else {
                    image.insert(image.end(), data, data + size);
                    received_bytes = image.size();
                }
            }
            return foundation::base::MakeSuccess();
        });
    if (!response.IsOk()) {
        return Result<FetchImageResult>(
            response.GetError(),
            response.GetMessage());
    }
    if (task_message.image_bytes > 0 && received_bytes != image.size()) {
        image.resize(received_bytes);
    }
    FetchImageResult result;
    result.image.swap(image);
    result.http_info = response.Value();
    return Result<FetchImageResult>(std::move(result));
}

Result<void> WriteBinaryFile(
    const std::string& path,
    const std::vector<char>& data) {
    const std::string parent = module_context::examples::task_flow::ParentPath(path);
    Result<void> ensure_result = EnsureDirectory(parent);
    if (!ensure_result.IsOk()) {
        return ensure_result;
    }

    std::ofstream output(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!output) {
        return Result<void>(
            ErrorCode::kIoError,
            "failed to open algorithm input image: " + path);
    }
    if (!data.empty()) {
        output.write(&data[0], static_cast<std::streamsize>(data.size()));
    }
    if (!output.good()) {
        return Result<void>(
            ErrorCode::kIoError,
            "failed to write algorithm input image: " + path);
    }
    return foundation::base::MakeSuccess();
}

Result<std::string> PrepareAlgorithmInputPath(
    const WorkerConfig& config,
    const TaskMessage& task_message,
    const std::vector<char>& image) {
    if (!config.algorithm_persist_input_images) {
        return Result<std::string>(task_message.image_id);
    }

    const std::string file_name =
        SanitizeFileStem(task_message.image_id) + ".bmp";
    const std::string image_path =
        module_context::examples::task_flow::JoinPath(
            config.algorithm_input_dir,
            file_name);
    Result<void> write_result = WriteBinaryFile(image_path, image);
    if (!write_result.IsOk()) {
        return Result<std::string>(
            write_result.GetError(),
            write_result.GetMessage());
    }
    return Result<std::string>(image_path);
}

Result<void> RunAlgorithmPlugin(
    const WorkerConfig& config,
    const TaskMessage& task_message,
    const std::vector<char>& image,
    Hh::Api::Plugin::IPluginTGVEtching* algorithm_plugin) {
    if (algorithm_plugin == NULL) {
        return foundation::base::MakeSuccess();
    }

    Result<std::string> input_path =
        PrepareAlgorithmInputPath(config, task_message, image);
    if (!input_path.IsOk()) {
        return Result<void>(
            input_path.GetError(),
            input_path.GetMessage());
    }

    try {
        std::string image_path = input_path.Value();
        (void)algorithm_plugin->AlignDetect(image_path);
    } catch (...) {
        return Result<void>(
            ErrorCode::kOperationFailed,
            "algorithm plugin threw an exception for image " +
                task_message.image_id);
    }

    return foundation::base::MakeSuccess();
}

TaskOutcome ProcessTask(
    const WorkerConfig& config,
    module_context::messaging::IMessageBusService* bus,
    module_context::http::IHttpTransferService* http,
    Hh::Api::Plugin::IPluginTGVEtching* algorithm_plugin,
    const TaskMessage& task_message,
    const SteadyClock::time_point& received_time) {
    TaskOutcome outcome;

    ResultMessage result_message;
    result_message.kind = kResultKindData;
    result_message.task_id = task_message.task_id;
    result_message.image_id = task_message.image_id;
    result_message.source_index = task_message.source_index;
    result_message.worker_id = config.worker_id;
    result_message.status = "processed";
    result_message.processed_bytes = 0;

    const SteadyClock::time_point work_begin = SteadyClock::now();
    result_message.worker_queue_ms = ElapsedMillis(work_begin, received_time);

    const SteadyClock::time_point fetch_begin = SteadyClock::now();
    Result<FetchImageResult> fetch_result = FetchImage(http, config, task_message);
    const SteadyClock::time_point fetch_end = SteadyClock::now();
    result_message.image_fetch_ms = ElapsedMillis(fetch_end, fetch_begin);

    if (!fetch_result.IsOk()) {
        if (IsRecoverableFetchError(fetch_result.GetError())) {
            std::ostringstream output;
            output << "[" << config.worker_id
                   << "] requeue task after HTTP fetch failure for "
                   << task_message.image_id << ": " << fetch_result.GetMessage();
            LogErrorLine(output.str());
            outcome.action = ConsumeAction::Requeue;
            return outcome;
        }
        result_message.status = "image_fetch_failed";
        result_message.detail_message = fetch_result.GetMessage();
    } else {
        const FetchImageResult& fetched = fetch_result.Value();
        result_message.processed_bytes = fetched.image.size();
        result_message.http_setup_ms = fetched.http_info.setup_ms;
        result_message.http_header_wait_ms = fetched.http_info.header_wait_ms;
        result_message.http_first_byte_ms = fetched.http_info.first_byte_ms;
        result_message.http_body_ms = fetched.http_info.body_ms;
        result_message.http_chunk_callback_ms =
            fetched.http_info.chunk_callback_ms;
        result_message.http_total_ms = fetched.http_info.total_ms;
        result_message.http_chunk_count = fetched.http_info.chunk_count;
        if (task_message.image_bytes != 0 &&
            task_message.image_bytes != result_message.processed_bytes) {
            result_message.status = "image_size_mismatch";
            result_message.detail_message =
                "expected image bytes does not match HTTP payload size";
        }
    }

    const SteadyClock::time_point algorithm_begin = SteadyClock::now();
    if (result_message.status == "processed" && algorithm_plugin != NULL) {
        Result<void> algorithm_result = RunAlgorithmPlugin(
            config,
            task_message,
            fetch_result.Value().image,
            algorithm_plugin);
        if (!algorithm_result.IsOk()) {
            result_message.status = "algorithm_failed";
            result_message.detail_message = algorithm_result.GetMessage();
        }
    }
    const SteadyClock::time_point algorithm_end = SteadyClock::now();
    result_message.algorithm_ms = ElapsedMillis(algorithm_end, algorithm_begin);

    const SteadyClock::time_point publish_begin = SteadyClock::now();
    result_message.worker_publish_ms = 0.0;
    result_message.worker_total_ms = ElapsedMillis(publish_begin, received_time);
    Result<void> publish_result = PublishResultMessage(
        bus,
        result_message);
    const SteadyClock::time_point publish_end = SteadyClock::now();
    result_message.worker_publish_ms = ElapsedMillis(publish_end, publish_begin);
    result_message.worker_total_ms = ElapsedMillis(publish_end, received_time);

    if (!publish_result.IsOk()) {
        std::ostringstream output;
        output << "[" << config.worker_id
               << "] failed to publish result for "
               << result_message.image_id << ": " << publish_result.GetMessage();
        LogErrorLine(output.str());
        outcome.action = ConsumeAction::Requeue;
        return outcome;
    }

    {
        std::ostringstream output;
        output << "[" << config.worker_id
               << "] metric image_id=" << result_message.image_id
               << " image_fetch_ms=" << result_message.image_fetch_ms
               << " http_first_byte_ms=" << result_message.http_first_byte_ms
               << " http_body_ms=" << result_message.http_body_ms
               << " http_copy_ms=" << result_message.http_chunk_callback_ms
               << " http_chunks=" << result_message.http_chunk_count
               << " algorithm_ms=" << result_message.algorithm_ms
               << " worker_publish_ms=" << result_message.worker_publish_ms
               << " worker_total_ms=" << result_message.worker_total_ms
               << " status=" << result_message.status;
        LogInfoLine(output.str());
    }

    outcome.action =
        result_message.status == "processed" ? ConsumeAction::Ack : ConsumeAction::Reject;
    outcome.counted_as_processed = (result_message.status == "processed");
    return outcome;
}

}  // namespace

int main(int argc, char** argv) {
    const std::map<std::string, std::string> args = ParseArguments(argc, argv);

    Result<std::string> module_config = RequireArgument(args, "module-config");
    Result<std::string> worker_id = RequireArgument(args, "worker-id");
    Result<std::string> output_dir = RequireArgument(args, "output-dir");
    if (!module_config.IsOk() || !worker_id.IsOk() || !output_dir.IsOk()) {
        std::cerr << (module_config.IsOk()
                          ? (worker_id.IsOk()
                                 ? output_dir.GetMessage()
                                 : worker_id.GetMessage())
                          : module_config.GetMessage())
                  << std::endl;
        return 2;
    }

    WorkerConfig config;
    config.module_config_path = module_config.Value();
    config.worker_id = worker_id.Value();
    config.worker_dir = output_dir.Value();
    config.http_endpoint =
        GetOptionalString(args, "http-endpoint", "http://127.0.0.1:50080");
    config.http_route =
        GetOptionalString(args, "http-route", "/task-flow/images");
    config.worker_task_threads = ParseOptionalInt(args, "worker-task-threads", 64);
    config.algorithm_delay_ms = ParseOptionalInt(args, "algorithm-delay-ms", 10);
    config.algorithm_plugin_name =
        GetOptionalString(args, "algorithm-plugin-name", std::string());
    config.algorithm_persist_input_images =
        ParseOptionalBool(args, "algorithm-persist-input-images", false);
    config.algorithm_input_dir =
        GetOptionalString(
            args,
            "algorithm-input-dir",
            module_context::examples::task_flow::JoinPath(
                config.worker_dir,
                "algorithm_input"));
    config.timeout_ms = ParseOptionalInt(args, "timeout-ms", 180000);

    if (config.worker_task_threads <= 0 || config.algorithm_delay_ms < 0 ||
        config.timeout_ms < 0 || config.http_endpoint.empty() ||
        config.http_route.empty()) {
        std::cerr << "worker received invalid arguments" << std::endl;
        return 2;
    }

    Result<void> ensure_output_result = EnsureDirectory(config.worker_dir);
    if (!ensure_output_result.IsOk()) {
        LogErrorLine(
            std::string("worker failed to create worker directory: ") +
            ensure_output_result.GetMessage());
        return 1;
    }

    MessageBusHost host(config.module_config_path);
    Result<void> load_result = host.LoadModules();
    if (!load_result.IsOk()) {
        LogErrorLine(std::string("worker load modules failed: ") + load_result.GetMessage());
        return 1;
    }

    Result<void> init_result = host.Init();
    if (!init_result.IsOk()) {
        LogErrorLine(std::string("worker init failed: ") + init_result.GetMessage());
        (void)host.Fini();
        return 1;
    }

    Result<void> start_result = host.Start();
    if (!start_result.IsOk()) {
        LogErrorLine(
            std::string("worker start failed: ") + start_result.GetMessage());
        (void)host.Fini();
        return 1;
    }

    Result<module_context::messaging::IMessageBusService*> bus_result =
        host.BusService();
    Result<module_context::http::IHttpTransferService*> http_result =
        host.HttpService();
    if (!bus_result.IsOk() || !http_result.IsOk()) {
        LogErrorLine(
            std::string("worker required service unavailable: ") +
            (bus_result.IsOk() ? http_result.GetMessage() : bus_result.GetMessage()));
        (void)host.Stop();
        (void)host.Fini();
        return 1;
    }
    module_context::messaging::IMessageBusService* bus = bus_result.Value();
    module_context::http::IHttpTransferService* http = http_result.Value();

    Hh::Api::Plugin::IPluginTGVEtching* algorithm_plugin = NULL;
    if (!config.algorithm_plugin_name.empty()) {
        Result<module_context::plugin::IPluginManagerService*> plugin_service =
            host.PluginService();
        if (!plugin_service.IsOk()) {
            LogErrorLine(
                std::string("worker plugin manager service unavailable: ") +
                plugin_service.GetMessage());
            (void)host.Stop();
            (void)host.Fini();
            return 1;
        }

        Result<Hh::Api::Plugin::IPluginTGVEtching*> plugin =
            plugin_service.Value()->GetPlugin<Hh::Api::Plugin::IPluginTGVEtching>(
                config.algorithm_plugin_name);
        if (!plugin.IsOk()) {
            LogErrorLine(
                std::string("worker algorithm plugin unavailable: ") +
                plugin.GetMessage());
            (void)host.Stop();
            (void)host.Fini();
            return 1;
        }
        algorithm_plugin = plugin.Value();
        LogInfoLine(
            std::string("[") + config.worker_id +
            "] algorithm plugin ready: " + config.algorithm_plugin_name);
    }

    std::shared_ptr<WorkerState> worker_state(new WorkerState());
    std::vector<std::string> registered_consumers;
    std::function<void()> unregister_consumers =
        [bus, &registered_consumers]() {
            for (std::vector<std::string>::const_iterator it =
                     registered_consumers.begin();
                 it != registered_consumers.end();
                 ++it) {
                (void)bus->UnregisterConsumerHandler(*it);
            }
            registered_consumers.clear();
        };

    std::function<ConsumeAction(const IncomingMessage&)> task_handler =
        [bus, http, algorithm_plugin, worker_state, config](
            const IncomingMessage& incoming) {
            const std::string payload(incoming.payload.begin(), incoming.payload.end());
            Result<TaskMessage> task = ParseTaskMessage(payload);
            if (!task.IsOk()) {
                std::ostringstream output;
                output << "[" << config.worker_id
                       << "] failed to parse task payload: "
                       << task.GetMessage();
                LogErrorLine(output.str());
                return ConsumeAction::Reject;
            }

            if (task.Value().kind == kTaskKindShutdown) {
                return ConsumeAction::Ack;
            }

            if (task.Value().kind != kTaskKindTask) {
                return ConsumeAction::Reject;
            }

            const SteadyClock::time_point received_time = SteadyClock::now();
            {
                std::unique_lock<std::mutex> lock(worker_state->mutex);
                ++worker_state->active_jobs;
            }

            const TaskOutcome outcome =
                ProcessTask(
                    config,
                    bus,
                    http,
                    algorithm_plugin,
                    task.Value(),
                    received_time);
            {
                std::unique_lock<std::mutex> lock(worker_state->mutex);
                if (outcome.counted_as_processed) {
                    ++worker_state->processed_count;
                } else {
                    ++worker_state->failed_count;
                }
                --worker_state->active_jobs;
                worker_state->state_cv.notify_all();
            }

            return outcome.action;
        };

    std::function<ConsumeAction(const IncomingMessage&)> control_handler =
        [worker_state, config](const IncomingMessage& incoming) {
            const std::string payload(incoming.payload.begin(), incoming.payload.end());
            Result<TaskMessage> task = ParseTaskMessage(payload);
            if (!task.IsOk()) {
                LogErrorLine(
                    std::string("[") + config.worker_id +
                    "] failed to parse control payload: " +
                    task.GetMessage());
                return ConsumeAction::Reject;
            }
            if (task.Value().kind != kTaskKindShutdown) {
                return ConsumeAction::Reject;
            }

            {
                std::unique_lock<std::mutex> lock(worker_state->mutex);
                worker_state->shutdown_received = true;
                worker_state->state_cv.notify_all();
            }
            LogInfoLine(std::string("[") + config.worker_id + "] received shutdown");
            return ConsumeAction::Ack;
        };

    Result<void> register_result =
        bus->RegisterConsumerHandler(kControlConsumerName, control_handler);
    if (register_result.IsOk()) {
        registered_consumers.push_back(kControlConsumerName);
    }
    if (register_result.IsOk()) {
        register_result = bus->RegisterConsumerHandler(kTaskConsumerName, task_handler);
        if (register_result.IsOk()) {
            registered_consumers.push_back(kTaskConsumerName);
        }
    }

    if (!register_result.IsOk()) {
        LogErrorLine(
            std::string("worker failed to register consumer handler: ") +
            register_result.GetMessage());
        unregister_consumers();
        (void)host.Stop();
        (void)host.Fini();
        return 1;
    }

    Result<void> wait_result = WaitForConnected(bus, config.timeout_ms);
    if (!wait_result.IsOk()) {
        LogErrorLine(
            std::string("worker failed to connect RabbitMQ: ") +
            wait_result.GetMessage());
        unregister_consumers();
        (void)host.Stop();
        (void)host.Fini();
        return 1;
    }

    bool timed_out = false;
    {
        std::unique_lock<std::mutex> lock(worker_state->mutex);
        if (config.timeout_ms == 0) {
            while (!(worker_state->shutdown_received &&
                     worker_state->active_jobs == 0)) {
                worker_state->state_cv.wait(lock);
            }
        } else {
            const SteadyClock::time_point deadline =
                SteadyClock::now() + std::chrono::milliseconds(config.timeout_ms);
            while (!(worker_state->shutdown_received &&
                     worker_state->active_jobs == 0)) {
                if (worker_state->state_cv.wait_until(lock, deadline) ==
                    std::cv_status::timeout) {
                    timed_out = true;
                    break;
                }
            }
        }
    }

    unregister_consumers();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    Result<void> stop_result = host.Stop();
    Result<void> fini_result = host.Fini();

    if (!stop_result.IsOk()) {
        LogErrorLine(
            std::string("worker stop failed: ") + stop_result.GetMessage());
        return 1;
    }
    if (!fini_result.IsOk()) {
        LogErrorLine(
            std::string("worker fini failed: ") + fini_result.GetMessage());
        return 1;
    }

    std::unique_lock<std::mutex> lock(worker_state->mutex);
    {
        std::ostringstream output;
        output << "[" << config.worker_id << "] processed_count="
               << worker_state->processed_count << ", failed_count="
               << worker_state->failed_count << ", shutdown_received="
               << (worker_state->shutdown_received ? "true" : "false");
        LogInfoLine(output.str());
    }

    return (!timed_out && worker_state->shutdown_received) ? 0 : 1;
}
