#include "common.h"

#include "foundation/base/ErrorCode.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif
#include <cctype>
#include <thread>

namespace module_context {
namespace examples {
namespace task_flow {

const char kTaskKindTask[] = "task";
const char kTaskKindShutdown[] = "shutdown";
const char kResultKindData[] = "data";

namespace {

using foundation::base::ErrorCode;
using foundation::base::Result;
using module_context::messaging::ConnectionState;

Result<void> MakeError(ErrorCode code, const std::string& message) {
    return Result<void>(code, message);
}

Result<std::string> MakeStringError(ErrorCode code, const std::string& message) {
    return Result<std::string>(code, message);
}

Result<TaskMessage> MakeTaskError(ErrorCode code, const std::string& message) {
    return Result<TaskMessage>(code, message);
}

Result<ResultMessage> MakeResultError(ErrorCode code, const std::string& message) {
    return Result<ResultMessage>(code, message);
}

bool DirectoryExists(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    struct stat info;
    if (stat(path.c_str(), &info) != 0) {
        return false;
    }

    return (info.st_mode & S_IFDIR) != 0;
}

int MakeSingleDirectory(const std::string& path) {
#if defined(_WIN32)
    return _mkdir(path.c_str());
#else
    return mkdir(path.c_str(), 0755);
#endif
}

std::string Trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           (value[begin] == ' ' || value[begin] == '\t' ||
            value[begin] == '\r' || value[begin] == '\n')) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin &&
           (value[end - 1] == ' ' || value[end - 1] == '\t' ||
            value[end - 1] == '\r' || value[end - 1] == '\n')) {
        --end;
    }

    return value.substr(begin, end - begin);
}

std::string ToLowerAscii(const std::string& value) {
    std::string normalized = value;
    for (std::size_t index = 0; index < normalized.size(); ++index) {
        if (normalized[index] >= 'A' && normalized[index] <= 'Z') {
            normalized[index] =
                static_cast<char>(normalized[index] - 'A' + 'a');
        }
    }
    return normalized;
}

std::string FormatDouble(double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(3) << value;
    return output.str();
}

bool TryParseInt(const std::string& text, int* value) {
    if (value == NULL || text.empty()) {
        return false;
    }

    char* end = NULL;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || (end != NULL && *end != '\0')) {
        return false;
    }

    *value = static_cast<int>(parsed);
    return true;
}

bool TryParseSize(const std::string& text, std::size_t* value) {
    if (value == NULL || text.empty()) {
        return false;
    }

    char* end = NULL;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
    if (end == text.c_str() || (end != NULL && *end != '\0')) {
        return false;
    }

    *value = static_cast<std::size_t>(parsed);
    return true;
}

bool TryParseDouble(const std::string& text, double* value) {
    if (value == NULL || text.empty()) {
        return false;
    }

    char* end = NULL;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || (end != NULL && *end != '\0')) {
        return false;
    }

    *value = parsed;
    return true;
}

std::string GetRequiredField(
    const std::map<std::string, std::string>& fields,
    const std::string& key,
    std::string* missing_key) {
    std::map<std::string, std::string>::const_iterator it = fields.find(key);
    if (it == fields.end()) {
        if (missing_key != NULL) {
            *missing_key = key;
        }
        return std::string();
    }

    return it->second;
}

std::map<std::string, std::string> ParseKeyValuePayload(
    const std::string& payload) {
    std::map<std::string, std::string> fields;
    std::istringstream stream(payload);
    std::string line;
    while (std::getline(stream, line)) {
        const std::size_t delimiter = line.find('=');
        if (delimiter == std::string::npos) {
            continue;
        }

        const std::string key = Trim(line.substr(0, delimiter));
        const std::string value = Trim(line.substr(delimiter + 1));
        if (!key.empty()) {
            fields[key] = value;
        }
    }

    return fields;
}

}  // namespace

MessageBusHost::MessageBusHost(const std::string& module_config_path)
    : module_config_path_(module_config_path),
      context_() {
}

MessageBusHost::~MessageBusHost() {
}

Result<void> MessageBusHost::LoadModules() {
    if (module_config_path_.empty()) {
        return MakeError(
            ErrorCode::kInvalidArgument,
            "module config path must not be empty");
    }

    module_context::framework::IModuleManager* manager = context_.ModuleManager();
    if (manager == NULL) {
        return MakeError(
            ErrorCode::kInvalidState,
            "Context does not expose a module manager");
    }

    return manager->LoadModules(module_config_path_);
}

Result<void> MessageBusHost::Init() {
    return context_.Init();
}

Result<void> MessageBusHost::Start() {
    return context_.Start();
}

Result<void> MessageBusHost::Stop() {
    return context_.Stop();
}

Result<void> MessageBusHost::Fini() {
    return context_.Fini();
}

Result<module_context::messaging::IMessageBusService*> MessageBusHost::BusService() {
    return context_.GetService<module_context::messaging::IMessageBusService>();
}

Result<module_context::http::IHttpTransferService*> MessageBusHost::HttpService() {
    return context_.GetService<module_context::http::IHttpTransferService>();
}

Result<module_context::plugin::IPluginManagerService*> MessageBusHost::PluginService() {
    return context_.GetService<module_context::plugin::IPluginManagerService>();
}

Result<void> WaitForConnected(
    module_context::messaging::IMessageBusService* bus,
    int timeout_ms) {
    if (bus == NULL) {
        return MakeError(ErrorCode::kInvalidArgument, "IMessageBusService is null");
    }

    if (timeout_ms < 0) {
        return MakeError(
            ErrorCode::kInvalidArgument,
            "timeout_ms must be zero or a positive integer");
    }

    if (timeout_ms == 0) {
        while (true) {
            if (bus->GetConnectionState() == ConnectionState::Connected) {
                return foundation::base::MakeSuccess();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        if (bus->GetConnectionState() == ConnectionState::Connected) {
            return foundation::base::MakeSuccess();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::ostringstream message;
    message << "Timed out waiting for RabbitMQ connection, last state="
            << static_cast<int>(bus->GetConnectionState());
    return MakeError(ErrorCode::kTimeout, message.str());
}

Result<void> EnsureDirectory(const std::string& path) {
    if (path.empty() || path == ".") {
        return foundation::base::MakeSuccess();
    }

    const std::string normalized = NormalizePath(path);
    if (DirectoryExists(normalized)) {
        return foundation::base::MakeSuccess();
    }

    const std::string parent = ParentPath(normalized);
    if (!parent.empty() && parent != normalized) {
        Result<void> parent_result = EnsureDirectory(parent);
        if (!parent_result.IsOk()) {
            return parent_result;
        }
    }

    if (MakeSingleDirectory(normalized) != 0 && errno != EEXIST) {
        return MakeError(
            ErrorCode::kInvalidState,
            "Failed to create directory '" + normalized + "', errno=" +
                std::to_string(errno));
    }

    return foundation::base::MakeSuccess();
}

Result<void> WriteTextFile(
    const std::string& path,
    const std::string& content) {
    Result<void> ensure_result = EnsureDirectory(ParentPath(path));
    if (!ensure_result.IsOk()) {
        return ensure_result;
    }

    std::ofstream output(path.c_str(), std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        return MakeError(
            ErrorCode::kInvalidState,
            "Failed to open text file '" + path + "'");
    }

    output << content;
    output.close();

    if (!output.good()) {
        return MakeError(
            ErrorCode::kInvalidState,
            "Failed to write text file '" + path + "'");
    }

    return foundation::base::MakeSuccess();
}

std::vector<char> CreatePatternBuffer(
    std::size_t size_bytes,
    std::uint32_t seed) {
    std::vector<char> data(size_bytes);
    for (std::size_t index = 0; index < size_bytes; ++index) {
        const std::uint32_t value =
            seed + static_cast<std::uint32_t>(index);
        data[index] = static_cast<char>(value % 251);
    }
    return data;
}

std::string ParentPath(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }

    const std::string normalized = NormalizePath(path);
    const std::size_t delimiter = normalized.find_last_of("/\\");
    if (delimiter == std::string::npos) {
        return std::string();
    }

#if defined(_WIN32)
    if (delimiter == 2 && normalized.size() >= 3 && normalized[1] == ':' &&
        (normalized[2] == '\\' || normalized[2] == '/')) {
        return normalized.substr(0, 3);
    }
#endif

    if (delimiter == 0) {
        return normalized.substr(0, 1);
    }
    return normalized.substr(0, delimiter);
}

std::string JoinPath(const std::string& left, const std::string& right) {
    if (left.empty()) {
        return NormalizePath(right);
    }
    if (right.empty()) {
        return NormalizePath(left);
    }

    const std::string normalized_left = NormalizePath(left);
    const std::string normalized_right = NormalizePath(right);

#if defined(_WIN32)
    const char separator = '\\';
#else
    const char separator = '/';
#endif

    if (normalized_left[normalized_left.size() - 1] == separator ||
        normalized_left[normalized_left.size() - 1] == '/' ||
        normalized_left[normalized_left.size() - 1] == '\\') {
        return normalized_left + normalized_right;
    }

#if defined(_WIN32)
    if (normalized_left.size() == 2 && normalized_left[1] == ':') {
        return normalized_left + "\\" + normalized_right;
    }
#endif

    return normalized_left + separator + normalized_right;
}

std::string NormalizePath(const std::string& path) {
#if defined(_WIN32)
    std::string normalized = path;
    for (std::size_t index = 0; index < normalized.size(); ++index) {
        if (normalized[index] == '/') {
            normalized[index] = '\\';
        }
    }
    return normalized;
#else
    return path;
#endif
}

bool IsAbsolutePath(const std::string& path) {
    const std::string normalized = NormalizePath(path);
    if (normalized.empty()) {
        return false;
    }

#if defined(_WIN32)
    if (normalized.size() >= 2 &&
        normalized[0] == '\\' &&
        normalized[1] == '\\') {
        return true;
    }

    return normalized.size() >= 3 &&
           std::isalpha(static_cast<unsigned char>(normalized[0])) != 0 &&
           normalized[1] == ':' &&
           (normalized[2] == '\\' || normalized[2] == '/');
#else
    return normalized[0] == '/';
#endif
}

std::string SerializeTaskMessage(const TaskMessage& message) {
    std::ostringstream output;
    output << "kind=" << message.kind << "\n";
    output << "task_id=" << message.task_id << "\n";
    output << "run_id=" << message.run_id << "\n";
    output << "image_id=" << message.image_id << "\n";
    output << "token=" << message.token << "\n";
    output << "source_index=" << message.source_index << "\n";
    output << "image_bytes="
           << static_cast<unsigned long long>(message.image_bytes) << "\n";
    output << "http_read_buffer_bytes="
           << static_cast<unsigned long long>(message.http_read_buffer_bytes)
           << "\n";
    output << "http_write_buffer_bytes="
           << static_cast<unsigned long long>(message.http_write_buffer_bytes)
           << "\n";
    output << "http_socket_receive_buffer_bytes="
           << static_cast<unsigned long long>(
                  message.http_socket_receive_buffer_bytes)
           << "\n";
    output << "http_socket_send_buffer_bytes="
           << static_cast<unsigned long long>(
                  message.http_socket_send_buffer_bytes)
           << "\n";
    return output.str();
}

Result<TaskMessage> ParseTaskMessage(const std::string& payload) {
    const std::map<std::string, std::string> fields = ParseKeyValuePayload(payload);
    std::string missing_key;
    TaskMessage message;

    message.kind = GetRequiredField(fields, "kind", &missing_key);
    if (message.kind.empty()) {
        return MakeTaskError(
            ErrorCode::kParseError,
            "Task payload missing field '" + missing_key + "'");
    }
    message.task_id = GetRequiredField(fields, "task_id", &missing_key);
    if (message.task_id.empty()) {
        return MakeTaskError(
            ErrorCode::kParseError,
            "Task payload missing field '" + missing_key + "'");
    }
    message.run_id =
        fields.count("run_id") == 0 ? std::string() : fields.find("run_id")->second;
    message.image_id =
        fields.count("image_id") == 0
            ? message.task_id
            : fields.find("image_id")->second;
    message.token =
        fields.count("token") == 0 ? std::string() : fields.find("token")->second;

    const std::string source_index =
        GetRequiredField(fields, "source_index", &missing_key);
    if (source_index.empty() || !TryParseInt(source_index, &message.source_index)) {
        return MakeTaskError(
            ErrorCode::kParseError,
            "Task payload has invalid field 'source_index'");
    }

    const std::string image_bytes =
        GetRequiredField(fields, "image_bytes", &missing_key);
    if (image_bytes.empty() || !TryParseSize(image_bytes, &message.image_bytes)) {
        return MakeTaskError(
            ErrorCode::kParseError,
            "Task payload has invalid field 'image_bytes'");
    }
    const std::string http_read_buffer_bytes =
        fields.count("http_read_buffer_bytes") == 0
            ? "0"
            : fields.find("http_read_buffer_bytes")->second;
    const std::string http_write_buffer_bytes =
        fields.count("http_write_buffer_bytes") == 0
            ? "0"
            : fields.find("http_write_buffer_bytes")->second;
    const std::string http_socket_receive_buffer_bytes =
        fields.count("http_socket_receive_buffer_bytes") == 0
            ? "0"
            : fields.find("http_socket_receive_buffer_bytes")->second;
    const std::string http_socket_send_buffer_bytes =
        fields.count("http_socket_send_buffer_bytes") == 0
            ? "0"
            : fields.find("http_socket_send_buffer_bytes")->second;
    if (!TryParseSize(
            http_read_buffer_bytes,
            &message.http_read_buffer_bytes)) {
        return MakeTaskError(
            ErrorCode::kParseError,
            "Task payload has invalid field 'http_read_buffer_bytes'");
    }
    if (!TryParseSize(
            http_write_buffer_bytes,
            &message.http_write_buffer_bytes)) {
        return MakeTaskError(
            ErrorCode::kParseError,
            "Task payload has invalid field 'http_write_buffer_bytes'");
    }
    if (!TryParseSize(
            http_socket_receive_buffer_bytes,
            &message.http_socket_receive_buffer_bytes)) {
        return MakeTaskError(
            ErrorCode::kParseError,
            "Task payload has invalid field 'http_socket_receive_buffer_bytes'");
    }
    if (!TryParseSize(
            http_socket_send_buffer_bytes,
            &message.http_socket_send_buffer_bytes)) {
        return MakeTaskError(
            ErrorCode::kParseError,
            "Task payload has invalid field 'http_socket_send_buffer_bytes'");
    }

    if (message.kind == kTaskKindTask && message.image_id.empty()) {
        return MakeTaskError(
            ErrorCode::kParseError,
            "Task payload missing field 'image_id'");
    }
    if (message.kind == kTaskKindTask && message.token.empty()) {
        return MakeTaskError(
            ErrorCode::kParseError,
            "Task payload missing field 'token'");
    }

    return Result<TaskMessage>(message);
}

std::string SerializeResultMessage(const ResultMessage& message) {
    std::ostringstream output;
    output << "kind=" << message.kind << "\n";
    output << "task_id=" << message.task_id << "\n";
    output << "image_id=" << message.image_id << "\n";
    output << "source_index=" << message.source_index << "\n";
    output << "worker_id=" << message.worker_id << "\n";
    output << "status=" << message.status << "\n";
    output << "detail_message=" << message.detail_message << "\n";
    output << "processed_bytes="
           << static_cast<unsigned long long>(message.processed_bytes) << "\n";
    output << "worker_queue_ms=" << FormatDouble(message.worker_queue_ms) << "\n";
    output << "image_fetch_ms=" << FormatDouble(message.image_fetch_ms) << "\n";
    output << "http_setup_ms=" << FormatDouble(message.http_setup_ms) << "\n";
    output << "http_header_wait_ms="
           << FormatDouble(message.http_header_wait_ms) << "\n";
    output << "http_first_byte_ms="
           << FormatDouble(message.http_first_byte_ms) << "\n";
    output << "http_body_ms=" << FormatDouble(message.http_body_ms) << "\n";
    output << "http_chunk_callback_ms="
           << FormatDouble(message.http_chunk_callback_ms) << "\n";
    output << "http_total_ms=" << FormatDouble(message.http_total_ms) << "\n";
    output << "http_chunk_count="
           << static_cast<unsigned long long>(message.http_chunk_count) << "\n";
    output << "algorithm_ms=" << FormatDouble(message.algorithm_ms) << "\n";
    output << "worker_publish_ms=" << FormatDouble(message.worker_publish_ms)
           << "\n";
    output << "worker_total_ms=" << FormatDouble(message.worker_total_ms) << "\n";
    return output.str();
}

Result<ResultMessage> ParseResultMessage(const std::string& payload) {
    const std::map<std::string, std::string> fields = ParseKeyValuePayload(payload);
    std::string missing_key;
    ResultMessage message;

    message.kind = fields.count("kind") == 0
                       ? kResultKindData
                       : ToLowerAscii(fields.find("kind")->second);
    if (message.kind.empty()) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload has invalid field 'kind'");
    }

    message.task_id = GetRequiredField(fields, "task_id", &missing_key);
    if (message.task_id.empty()) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload missing field '" + missing_key + "'");
    }
    message.image_id =
        fields.count("image_id") == 0
            ? message.task_id
            : fields.find("image_id")->second;
    if (message.image_id.empty()) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload missing field 'image_id'");
    }

    const std::string source_index =
        GetRequiredField(fields, "source_index", &missing_key);
    if (source_index.empty() || !TryParseInt(source_index, &message.source_index)) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload has invalid field 'source_index'");
    }

    message.worker_id = GetRequiredField(fields, "worker_id", &missing_key);
    if (message.worker_id.empty()) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload missing field '" + missing_key + "'");
    }
    message.status = GetRequiredField(fields, "status", &missing_key);
    if (message.status.empty()) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload missing field '" + missing_key + "'");
    }
    message.detail_message =
        fields.count("detail_message") == 0
            ? std::string()
            : fields.find("detail_message")->second;

    const std::string processed_bytes =
        GetRequiredField(fields, "processed_bytes", &missing_key);
    if (processed_bytes.empty() ||
        !TryParseSize(processed_bytes, &message.processed_bytes)) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload has invalid field 'processed_bytes'");
    }

    const std::string worker_queue_ms =
        GetRequiredField(fields, "worker_queue_ms", &missing_key);
    const std::string image_fetch_ms =
        GetRequiredField(fields, "image_fetch_ms", &missing_key);
    const std::string http_setup_ms =
        fields.count("http_setup_ms") == 0
            ? std::string("0")
            : fields.find("http_setup_ms")->second;
    const std::string http_header_wait_ms =
        fields.count("http_header_wait_ms") == 0
            ? std::string("0")
            : fields.find("http_header_wait_ms")->second;
    const std::string http_first_byte_ms =
        fields.count("http_first_byte_ms") == 0
            ? std::string("0")
            : fields.find("http_first_byte_ms")->second;
    const std::string http_body_ms =
        fields.count("http_body_ms") == 0
            ? std::string("0")
            : fields.find("http_body_ms")->second;
    const std::string http_chunk_callback_ms =
        fields.count("http_chunk_callback_ms") == 0
            ? std::string("0")
            : fields.find("http_chunk_callback_ms")->second;
    const std::string http_total_ms =
        fields.count("http_total_ms") == 0
            ? std::string("0")
            : fields.find("http_total_ms")->second;
    const std::string http_chunk_count =
        fields.count("http_chunk_count") == 0
            ? std::string("0")
            : fields.find("http_chunk_count")->second;
    const std::string algorithm_ms =
        GetRequiredField(fields, "algorithm_ms", &missing_key);
    const std::string worker_publish_ms =
        fields.count("worker_publish_ms") == 0
            ? std::string("0")
            : fields.find("worker_publish_ms")->second;
    const std::string worker_total_ms =
        GetRequiredField(fields, "worker_total_ms", &missing_key);

    if (worker_queue_ms.empty() ||
        !TryParseDouble(worker_queue_ms, &message.worker_queue_ms)) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload has invalid field 'worker_queue_ms'");
    }
    if (image_fetch_ms.empty() ||
        !TryParseDouble(image_fetch_ms, &message.image_fetch_ms)) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload has invalid field 'image_fetch_ms'");
    }
    if (http_setup_ms.empty() ||
        !TryParseDouble(http_setup_ms, &message.http_setup_ms)) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload has invalid field 'http_setup_ms'");
    }
    if (http_header_wait_ms.empty() ||
        !TryParseDouble(http_header_wait_ms, &message.http_header_wait_ms)) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload has invalid field 'http_header_wait_ms'");
    }
    if (http_first_byte_ms.empty() ||
        !TryParseDouble(http_first_byte_ms, &message.http_first_byte_ms)) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload has invalid field 'http_first_byte_ms'");
    }
    if (http_body_ms.empty() ||
        !TryParseDouble(http_body_ms, &message.http_body_ms)) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload has invalid field 'http_body_ms'");
    }
    if (http_chunk_callback_ms.empty() ||
        !TryParseDouble(
            http_chunk_callback_ms,
            &message.http_chunk_callback_ms)) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload has invalid field 'http_chunk_callback_ms'");
    }
    if (http_total_ms.empty() ||
        !TryParseDouble(http_total_ms, &message.http_total_ms)) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload has invalid field 'http_total_ms'");
    }
    if (http_chunk_count.empty() ||
        !TryParseSize(http_chunk_count, &message.http_chunk_count)) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload has invalid field 'http_chunk_count'");
    }
    if (algorithm_ms.empty() ||
        !TryParseDouble(algorithm_ms, &message.algorithm_ms)) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload has invalid field 'algorithm_ms'");
    }
    if (worker_publish_ms.empty() ||
        !TryParseDouble(worker_publish_ms, &message.worker_publish_ms)) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload has invalid field 'worker_publish_ms'");
    }
    if (worker_total_ms.empty() ||
        !TryParseDouble(worker_total_ms, &message.worker_total_ms)) {
        return MakeResultError(
            ErrorCode::kParseError,
            "Result payload has invalid field 'worker_total_ms'");
    }

    return Result<ResultMessage>(message);
}

Result<std::string> RequireArgument(
    const std::map<std::string, std::string>& args,
    const std::string& key) {
    std::map<std::string, std::string>::const_iterator it = args.find(key);
    if (it == args.end() || it->second.empty()) {
        return MakeStringError(
            ErrorCode::kInvalidArgument,
            "Missing required argument '--" + key + "'");
    }
    return Result<std::string>(it->second);
}

int ParseOptionalInt(
    const std::map<std::string, std::string>& args,
    const std::string& key,
    int fallback_value) {
    std::map<std::string, std::string>::const_iterator it = args.find(key);
    if (it == args.end() || it->second.empty()) {
        return fallback_value;
    }

    int parsed = fallback_value;
    if (!TryParseInt(it->second, &parsed)) {
        return fallback_value;
    }
    return parsed;
}

std::size_t ParseOptionalSize(
    const std::map<std::string, std::string>& args,
    const std::string& key,
    std::size_t fallback_value) {
    std::map<std::string, std::string>::const_iterator it = args.find(key);
    if (it == args.end() || it->second.empty()) {
        return fallback_value;
    }

    std::size_t parsed = fallback_value;
    if (!TryParseSize(it->second, &parsed)) {
        return fallback_value;
    }
    return parsed;
}

std::map<std::string, std::string> ParseArguments(int argc, char** argv) {
    std::map<std::string, std::string> args;
    for (int index = 1; index < argc; ++index) {
        const std::string token(argv[index]);
        if (token.size() <= 2 || token[0] != '-' || token[1] != '-') {
            continue;
        }

        const std::string key = token.substr(2);
        std::string value = "1";
        if (index + 1 < argc) {
            const std::string next(argv[index + 1]);
            if (!(next.size() > 1 && next[0] == '-' && next[1] == '-')) {
                value = next;
                ++index;
            }
        }
        args[key] = value;
    }
    return args;
}

}  // namespace task_flow
}  // namespace examples
}  // namespace module_context
