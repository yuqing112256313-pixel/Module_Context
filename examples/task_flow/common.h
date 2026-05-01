#pragma once

#include "framework/Context.h"

#include "module_context/framework/IModuleManager.h"
#include "module_context/http/IHttpTransferService.h"
#include "module_context/messaging/IMessageBusService.h"
#include "module_context/plugin/IPluginManagerService.h"

#include "foundation/base/Result.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace module_context {
namespace examples {
namespace task_flow {

extern const char kTaskKindTask[];
extern const char kTaskKindShutdown[];
extern const char kResultKindData[];

struct TaskMessage {
    std::string kind;
    std::string task_id;
    std::string run_id;
    std::string image_id;
    std::string token;
    int source_index;
    std::size_t image_bytes;
    std::size_t http_read_buffer_bytes;
    std::size_t http_write_buffer_bytes;
    std::size_t http_socket_receive_buffer_bytes;
    std::size_t http_socket_send_buffer_bytes;

    TaskMessage()
        : kind(),
          task_id(),
          run_id(),
          image_id(),
          token(),
          source_index(-1),
          image_bytes(0),
          http_read_buffer_bytes(0),
          http_write_buffer_bytes(0),
          http_socket_receive_buffer_bytes(0),
          http_socket_send_buffer_bytes(0) {
    }
};

struct ResultMessage {
    std::string kind;
    std::string task_id;
    std::string image_id;
    int source_index;
    std::string worker_id;
    std::string status;
    std::string detail_message;
    std::size_t processed_bytes;
    double worker_queue_ms;
    double image_fetch_ms;
    double http_setup_ms;
    double http_header_wait_ms;
    double http_first_byte_ms;
    double http_body_ms;
    double http_chunk_callback_ms;
    double http_total_ms;
    std::size_t http_chunk_count;
    double algorithm_ms;
    double worker_publish_ms;
    double worker_total_ms;

    ResultMessage()
        : kind(),
          task_id(),
          image_id(),
          source_index(-1),
          worker_id(),
          status(),
          detail_message(),
          processed_bytes(0),
          worker_queue_ms(0.0),
          image_fetch_ms(0.0),
          http_setup_ms(0.0),
          http_header_wait_ms(0.0),
          http_first_byte_ms(0.0),
          http_body_ms(0.0),
          http_chunk_callback_ms(0.0),
          http_total_ms(0.0),
          http_chunk_count(0),
          algorithm_ms(0.0),
          worker_publish_ms(0.0),
          worker_total_ms(0.0) {
    }
};

class MessageBusHost {
public:
    explicit MessageBusHost(const std::string& module_config_path);
    ~MessageBusHost();

    foundation::base::Result<void> LoadModules();
    foundation::base::Result<void> Init();
    foundation::base::Result<void> Start();
    foundation::base::Result<void> Stop();
    foundation::base::Result<void> Fini();
    foundation::base::Result<module_context::messaging::IMessageBusService*> BusService();
    foundation::base::Result<module_context::http::IHttpTransferService*> HttpService();
    foundation::base::Result<module_context::plugin::IPluginManagerService*> PluginService();

private:
    std::string module_config_path_;
    module_context::framework::Context context_;
};

foundation::base::Result<void> WaitForConnected(
    module_context::messaging::IMessageBusService* bus,
    int timeout_ms);

foundation::base::Result<void> EnsureDirectory(const std::string& path);
foundation::base::Result<void> WriteTextFile(
    const std::string& path,
    const std::string& content);
std::vector<char> CreatePatternBuffer(
    std::size_t size_bytes,
    std::uint32_t seed);

std::string ParentPath(const std::string& path);
std::string JoinPath(const std::string& left, const std::string& right);
std::string NormalizePath(const std::string& path);
bool IsAbsolutePath(const std::string& path);

std::string SerializeTaskMessage(const TaskMessage& message);
foundation::base::Result<TaskMessage> ParseTaskMessage(const std::string& payload);
std::string SerializeResultMessage(const ResultMessage& message);
foundation::base::Result<ResultMessage> ParseResultMessage(
    const std::string& payload);

foundation::base::Result<std::string> RequireArgument(
    const std::map<std::string, std::string>& args,
    const std::string& key);
int ParseOptionalInt(
    const std::map<std::string, std::string>& args,
    const std::string& key,
    int fallback_value);
std::size_t ParseOptionalSize(
    const std::map<std::string, std::string>& args,
    const std::string& key,
    std::size_t fallback_value);
std::map<std::string, std::string> ParseArguments(int argc, char** argv);

}  // namespace task_flow
}  // namespace examples
}  // namespace module_context
