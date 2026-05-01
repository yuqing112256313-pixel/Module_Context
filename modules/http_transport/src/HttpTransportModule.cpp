#include "HttpTransportModule.h"

#include "module_context/framework/IModuleManager.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/config/ConfigValue.h"
#include "plugin/ModuleFactory.h"

#include "httplib.h"
#ifdef GetMessage
#undef GetMessage
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <memory>
#include <sstream>

namespace module_context {
namespace http {

namespace {

using foundation::base::ErrorCode;
using foundation::base::Result;
using foundation::config::ConfigValue;
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

Result<void> MakeError(ErrorCode code, const std::string& message) {
    return Result<void>(code, message);
}

int ClampSocketBufferSize(std::size_t value) {
    const std::size_t max_int =
        static_cast<std::size_t>((std::numeric_limits<int>::max)());
    return static_cast<int>((std::min)(value, max_int));
}

void ConfigureSocketOptions(
    socket_t sock,
    std::size_t receive_buffer_bytes,
    std::size_t send_buffer_bytes) {
    httplib::default_socket_options(sock);
    if (receive_buffer_bytes > 0) {
        httplib::set_socket_opt(
            sock,
            SOL_SOCKET,
            SO_RCVBUF,
            ClampSocketBufferSize(receive_buffer_bytes));
    }
    if (send_buffer_bytes > 0) {
        httplib::set_socket_opt(
            sock,
            SOL_SOCKET,
            SO_SNDBUF,
            ClampSocketBufferSize(send_buffer_bytes));
    }
}

httplib::SocketOptions MakeSocketOptions(
    std::size_t receive_buffer_bytes,
    std::size_t send_buffer_bytes) {
    return [receive_buffer_bytes, send_buffer_bytes](socket_t sock) {
        ConfigureSocketOptions(sock, receive_buffer_bytes, send_buffer_bytes);
    };
}

std::size_t SelectSize(std::size_t preferred, std::size_t fallback) {
    return preferred == 0 ? fallback : preferred;
}

Result<HttpResponseInfo> MakeResponseError(
    ErrorCode code,
    const std::string& message) {
    return Result<HttpResponseInfo>(code, message);
}

std::string NormalizeRoute(const std::string& route) {
    if (route.empty()) {
        return std::string();
    }
    if (route[0] == '/') {
        return route;
    }
    return "/" + route;
}

int SecondsPart(int milliseconds) {
    return milliseconds <= 0 ? 0 : milliseconds / 1000;
}

int MicrosecondsPart(int milliseconds) {
    return milliseconds <= 0 ? 0 : (milliseconds % 1000) * 1000;
}

Result<std::string> GetOptionalString(
    const ConfigValue& root,
    const std::string& key,
    const std::string& fallback) {
    if (!root.IsObject() || !root.Contains(key)) {
        return Result<std::string>(fallback);
    }
    Result<ConfigValue> value = root.ObjectGet(key);
    if (!value.IsOk()) {
        return Result<std::string>(value.GetError(), value.GetMessage());
    }
    Result<std::string> parsed = value.Value().AsString();
    if (!parsed.IsOk()) {
        return Result<std::string>(
            ErrorCode::kParseError,
            "HTTP config field '" + key + "' must be a string");
    }
    return parsed;
}

Result<std::int64_t> GetOptionalInt64(
    const ConfigValue& root,
    const std::string& key,
    std::int64_t fallback) {
    if (!root.IsObject() || !root.Contains(key)) {
        return Result<std::int64_t>(fallback);
    }
    Result<ConfigValue> value = root.ObjectGet(key);
    if (!value.IsOk()) {
        return Result<std::int64_t>(value.GetError(), value.GetMessage());
    }
    Result<std::int64_t> parsed = value.Value().AsInt64();
    if (!parsed.IsOk()) {
        return Result<std::int64_t>(
            ErrorCode::kParseError,
            "HTTP config field '" + key + "' must be an integer");
    }
    return parsed;
}

Result<HttpTransportConfig> ParseConfig(const ConfigValue& root) {
    HttpTransportConfig config;
    Result<std::string> role = GetOptionalString(root, "role", config.role);
    Result<std::string> listen_address =
        GetOptionalString(root, "listen_address", config.listen_address);
    Result<std::string> endpoint =
        GetOptionalString(root, "endpoint", config.endpoint);
    Result<std::int64_t> port =
        GetOptionalInt64(root, "port", config.port);
    Result<std::int64_t> server_thread_count =
        GetOptionalInt64(root, "server_thread_count", config.server_thread_count);
    Result<std::int64_t> read_timeout_ms =
        GetOptionalInt64(root, "read_timeout_ms", config.read_timeout_ms);
    Result<std::int64_t> write_timeout_ms =
        GetOptionalInt64(root, "write_timeout_ms", config.write_timeout_ms);
    Result<std::int64_t> max_payload_bytes =
        GetOptionalInt64(root, "max_payload_bytes", config.max_payload_bytes);
    Result<std::int64_t> chunk_bytes =
        GetOptionalInt64(root, "chunk_bytes", config.chunk_bytes);
    Result<std::int64_t> read_buffer_bytes =
        GetOptionalInt64(root, "read_buffer_bytes", config.read_buffer_bytes);
    Result<std::int64_t> write_buffer_bytes =
        GetOptionalInt64(root, "write_buffer_bytes", config.write_buffer_bytes);
    Result<std::int64_t> socket_receive_buffer_bytes =
        GetOptionalInt64(
            root,
            "socket_receive_buffer_bytes",
            config.socket_receive_buffer_bytes);
    Result<std::int64_t> socket_send_buffer_bytes =
        GetOptionalInt64(
            root,
            "socket_send_buffer_bytes",
            config.socket_send_buffer_bytes);

    if (!role.IsOk()) {
        return Result<HttpTransportConfig>(role.GetError(), role.GetMessage());
    }
    if (!listen_address.IsOk()) {
        return Result<HttpTransportConfig>(
            listen_address.GetError(),
            listen_address.GetMessage());
    }
    if (!endpoint.IsOk()) {
        return Result<HttpTransportConfig>(
            endpoint.GetError(),
            endpoint.GetMessage());
    }
    if (!port.IsOk() || !server_thread_count.IsOk() ||
        !read_timeout_ms.IsOk() || !write_timeout_ms.IsOk() ||
        !max_payload_bytes.IsOk() || !chunk_bytes.IsOk() ||
        !read_buffer_bytes.IsOk() || !write_buffer_bytes.IsOk() ||
        !socket_receive_buffer_bytes.IsOk() ||
        !socket_send_buffer_bytes.IsOk()) {
        return Result<HttpTransportConfig>(
            ErrorCode::kParseError,
            "HTTP config numeric field is invalid");
    }

    config.role = role.Value();
    config.listen_address = listen_address.Value();
    config.endpoint = endpoint.Value();
    config.port = static_cast<int>(port.Value());
    config.server_thread_count = static_cast<int>(server_thread_count.Value());
    config.read_timeout_ms = static_cast<int>(read_timeout_ms.Value());
    config.write_timeout_ms = static_cast<int>(write_timeout_ms.Value());
    config.max_payload_bytes =
        static_cast<std::size_t>(max_payload_bytes.Value());
    config.chunk_bytes = static_cast<std::size_t>(chunk_bytes.Value());
    config.read_buffer_bytes =
        static_cast<std::size_t>(read_buffer_bytes.Value());
    config.write_buffer_bytes =
        static_cast<std::size_t>(write_buffer_bytes.Value());
    config.socket_receive_buffer_bytes =
        static_cast<std::size_t>(socket_receive_buffer_bytes.Value());
    config.socket_send_buffer_bytes =
        static_cast<std::size_t>(socket_send_buffer_bytes.Value());

    if (config.role != "server" && config.role != "client" &&
        config.role != "both") {
        return Result<HttpTransportConfig>(
            ErrorCode::kParseError,
            "HTTP config field 'role' must be server, client, or both");
    }
    if (config.port <= 0 || config.port > 65535 ||
        config.server_thread_count <= 0 ||
        config.read_timeout_ms < 0 ||
        config.write_timeout_ms < 0 ||
        config.max_payload_bytes == 0 ||
        config.chunk_bytes == 0 ||
        read_buffer_bytes.Value() < 0 ||
        write_buffer_bytes.Value() < 0 ||
        socket_receive_buffer_bytes.Value() < 0 ||
        socket_send_buffer_bytes.Value() < 0) {
        return Result<HttpTransportConfig>(
            ErrorCode::kParseError,
            "HTTP config contains invalid numeric values");
    }
    if ((config.role == "server" || config.role == "both") &&
        config.listen_address.empty()) {
        return Result<HttpTransportConfig>(
            ErrorCode::kParseError,
            "HTTP server listen_address must not be empty");
    }
    if ((config.role == "client" || config.role == "both") &&
        config.endpoint.empty()) {
        return Result<HttpTransportConfig>(
            ErrorCode::kParseError,
            "HTTP client endpoint must not be empty");
    }

    return Result<HttpTransportConfig>(config);
}

httplib::Headers ToHttplibHeaders(const HeaderMap& headers) {
    httplib::Headers converted;
    HeaderMap::const_iterator it = headers.begin();
    for (; it != headers.end(); ++it) {
        converted.insert(std::make_pair(it->first, it->second));
    }
    return converted;
}

HeaderMap FromHttplibHeaders(const httplib::Headers& headers) {
    HeaderMap converted;
    httplib::Headers::const_iterator it = headers.begin();
    for (; it != headers.end(); ++it) {
        converted[it->first] = it->second;
    }
    return converted;
}

HttpServerRequest MakeServerRequest(const httplib::Request& request) {
    HttpServerRequest converted;
    converted.route = request.path;
    converted.headers = FromHttplibHeaders(request.headers);
    converted.remote_address = request.remote_addr;
    return converted;
}

int ErrorCodeToHttpStatus(ErrorCode code) {
    switch (code) {
        case ErrorCode::kInvalidArgument:
        case ErrorCode::kParseError:
            return 400;
        case ErrorCode::kPermissionDenied:
            return 401;
        case ErrorCode::kNotFound:
            return 404;
        case ErrorCode::kTimeout:
            return 504;
        case ErrorCode::kBusy:
            return 503;
        default:
            return 500;
    }
}

ErrorCode HttpStatusToErrorCode(int status) {
    if (status == 400) {
        return ErrorCode::kInvalidArgument;
    }
    if (status == 401 || status == 403) {
        return ErrorCode::kPermissionDenied;
    }
    if (status == 404) {
        return ErrorCode::kNotFound;
    }
    if (status == 408 || status == 504) {
        return ErrorCode::kTimeout;
    }
    if (status == 503) {
        return ErrorCode::kBusy;
    }
    return ErrorCode::kOperationFailed;
}

std::string BuildHttpStatusMessage(int status) {
    std::ostringstream output;
    output << "HTTP request failed with status " << status;
    return output.str();
}

}  // namespace

HttpTransportModule::HttpTransportModule()
    : mutex_(),
      config_(),
      download_handlers_(),
      upload_handlers_(),
      snapshot_(),
      server_(),
      server_thread_() {
}

HttpTransportModule::~HttpTransportModule() {
    (void)OnStop();
}

std::string HttpTransportModule::ModuleType() const {
    return "http_transport";
}

std::string HttpTransportModule::ModuleVersion() const {
    return "1.0.0";
}

Result<void> HttpTransportModule::RegisterDownloadHandler(
    const std::string& route,
    DownloadHandler handler) {
    const std::string normalized = NormalizeRoute(route);
    if (normalized.empty()) {
        return MakeError(ErrorCode::kInvalidArgument, "HTTP route is empty");
    }
    if (!handler) {
        return MakeError(ErrorCode::kInvalidArgument, "Download handler is empty");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (download_handlers_.find(normalized) != download_handlers_.end() ||
        upload_handlers_.find(normalized) != upload_handlers_.end()) {
        return MakeError(
            ErrorCode::kAlreadyExists,
            "HTTP handler already exists for route '" + normalized + "'");
    }
    download_handlers_[normalized] = handler;
    return foundation::base::MakeSuccess();
}

Result<void> HttpTransportModule::RegisterUploadHandler(
    const std::string& route,
    UploadHandler handler) {
    const std::string normalized = NormalizeRoute(route);
    if (normalized.empty()) {
        return MakeError(ErrorCode::kInvalidArgument, "HTTP route is empty");
    }
    if (!handler) {
        return MakeError(ErrorCode::kInvalidArgument, "Upload handler is empty");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (download_handlers_.find(normalized) != download_handlers_.end() ||
        upload_handlers_.find(normalized) != upload_handlers_.end()) {
        return MakeError(
            ErrorCode::kAlreadyExists,
            "HTTP handler already exists for route '" + normalized + "'");
    }
    upload_handlers_[normalized] = handler;
    return foundation::base::MakeSuccess();
}

Result<void> HttpTransportModule::UnregisterHandler(const std::string& route) {
    const std::string normalized = NormalizeRoute(route);
    if (normalized.empty()) {
        return MakeError(ErrorCode::kInvalidArgument, "HTTP route is empty");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t erased =
        download_handlers_.erase(normalized) + upload_handlers_.erase(normalized);
    if (erased == 0) {
        return MakeError(
            ErrorCode::kNotFound,
            "HTTP handler not found for route '" + normalized + "'");
    }
    return foundation::base::MakeSuccess();
}

Result<HttpResponseInfo> HttpTransportModule::Download(
    const HttpDownloadRequest& request,
    HttpChunkHandler on_chunk) {
    if (!ClientRoleEnabled()) {
        return MakeResponseError(
            ErrorCode::kInvalidState,
            "HTTP transport is not configured for client role");
    }
    if (request.endpoint.empty() || request.route.empty()) {
        return MakeResponseError(
            ErrorCode::kInvalidArgument,
            "HTTP download endpoint and route must not be empty");
    }
    if (!on_chunk) {
        return MakeResponseError(
            ErrorCode::kInvalidArgument,
            "HTTP download chunk handler is empty");
    }

    AddActiveRequest();
    HttpResponseInfo info;
    Result<void> chunk_result = foundation::base::MakeSuccess();
    std::size_t received = 0;
    bool header_seen = false;
    bool first_chunk_seen = false;
    SteadyClock::time_point header_time;
    SteadyClock::time_point first_chunk_time;
    std::int64_t callback_microseconds = 0;
    const std::size_t read_buffer_bytes =
        SelectSize(request.read_buffer_bytes, config_.read_buffer_bytes);
    const std::size_t write_buffer_bytes =
        SelectSize(request.write_buffer_bytes, config_.write_buffer_bytes);
    const std::size_t socket_receive_buffer_bytes = SelectSize(
        request.socket_receive_buffer_bytes,
        config_.socket_receive_buffer_bytes);
    const std::size_t socket_send_buffer_bytes = SelectSize(
        request.socket_send_buffer_bytes,
        config_.socket_send_buffer_bytes);

    const SteadyClock::time_point download_begin = SteadyClock::now();
    httplib::set_runtime_buffer_sizes(read_buffer_bytes, write_buffer_bytes);
    httplib::Client client(request.endpoint);
    client.set_keep_alive(false);
    client.set_tcp_nodelay(true);
    client.set_socket_options(
        MakeSocketOptions(socket_receive_buffer_bytes, socket_send_buffer_bytes));
    const int timeout_ms =
        request.timeout_ms < 0 ? config_.read_timeout_ms : request.timeout_ms;
    client.set_read_timeout(SecondsPart(timeout_ms), MicrosecondsPart(timeout_ms));
    client.set_write_timeout(
        SecondsPart(config_.write_timeout_ms),
        MicrosecondsPart(config_.write_timeout_ms));
    client.set_payload_max_length(config_.max_payload_bytes);

    httplib::Headers headers = ToHttplibHeaders(request.headers);
    const std::string route = NormalizeRoute(request.route);
    const SteadyClock::time_point request_begin = SteadyClock::now();
    info.setup_ms = ElapsedMillis(request_begin, download_begin);
    httplib::ResponseHandler response_handler =
        [&info, &header_seen, &header_time, &request_begin](
            const httplib::Response& response) {
            header_seen = true;
            header_time = SteadyClock::now();
            info.status_code = response.status;
            info.headers = FromHttplibHeaders(response.headers);
            info.header_wait_ms = ElapsedMillis(header_time, request_begin);
            return true;
        };
    httplib::ContentReceiver content_receiver =
        [this,
         &chunk_result,
         &on_chunk,
         &received,
         &request,
         &callback_microseconds,
         &first_chunk_seen,
         &first_chunk_time,
         &info](
            const char* data,
            std::size_t size) {
            if (!chunk_result.IsOk()) {
                return false;
            }
            if (request.max_bytes != 0 && received + size > request.max_bytes) {
                chunk_result = Result<void>(
                    ErrorCode::kOutOfRange,
                    "HTTP download exceeded max_bytes");
                return false;
            }
            const SteadyClock::time_point callback_begin = SteadyClock::now();
            chunk_result = on_chunk(data, size);
            const SteadyClock::time_point callback_end = SteadyClock::now();
            callback_microseconds +=
                std::chrono::duration_cast<std::chrono::microseconds>(
                    callback_end - callback_begin)
                    .count();
            if (!chunk_result.IsOk()) {
                return false;
            }
            if (size > 0) {
                if (!first_chunk_seen) {
                    first_chunk_seen = true;
                    first_chunk_time = callback_begin;
                }
                ++info.chunk_count;
            }
            received += size;
            AddBytesReceived(size);
            return true;
        };
    httplib::Result result = client.Get(
        route.c_str(),
        headers,
        response_handler,
        content_receiver);

    const SteadyClock::time_point download_end = SteadyClock::now();
    info.bytes_transferred = received;
    info.total_ms = ElapsedMillis(download_end, download_begin);
    info.chunk_callback_ms =
        static_cast<double>(callback_microseconds) / 1000.0;
    if (!header_seen) {
        info.header_wait_ms = ElapsedMillis(download_end, request_begin);
    }
    if (first_chunk_seen) {
        info.first_byte_ms = ElapsedMillis(first_chunk_time, request_begin);
        info.body_ms = ElapsedMillis(download_end, first_chunk_time);
    }
    if (!result) {
        FinishRequest(false);
        if (!chunk_result.IsOk()) {
            return MakeResponseError(
                chunk_result.GetError(),
                chunk_result.GetMessage());
        }
        return MakeResponseError(
            ErrorCode::kIoError,
            httplib::to_string(result.error()));
    }

    info.status_code = result->status;
    info.status_message = httplib::status_message(result->status);
    info.headers = FromHttplibHeaders(result->headers);

    if (result->status < 200 || result->status >= 300) {
        FinishRequest(false);
        return MakeResponseError(
            HttpStatusToErrorCode(result->status),
            BuildHttpStatusMessage(result->status));
    }
    if (!chunk_result.IsOk()) {
        FinishRequest(false);
        return MakeResponseError(
            chunk_result.GetError(),
            chunk_result.GetMessage());
    }

    FinishRequest(true);
    return Result<HttpResponseInfo>(info);
}

Result<HttpResponseInfo> HttpTransportModule::Upload(
    const HttpUploadRequest& request,
    HttpUploadProvider provider,
    HttpChunkHandler on_response_chunk) {
    if (!ClientRoleEnabled()) {
        return MakeResponseError(
            ErrorCode::kInvalidState,
            "HTTP transport is not configured for client role");
    }
    if (request.endpoint.empty() || request.route.empty()) {
        return MakeResponseError(
            ErrorCode::kInvalidArgument,
            "HTTP upload endpoint and route must not be empty");
    }
    if (!provider) {
        return MakeResponseError(
            ErrorCode::kInvalidArgument,
            "HTTP upload provider is empty");
    }

    AddActiveRequest();
    Result<void> provider_result = foundation::base::MakeSuccess();
    Result<void> response_result = foundation::base::MakeSuccess();
    HttpResponseInfo info;
    std::size_t response_bytes = 0;
    const std::size_t read_buffer_bytes =
        SelectSize(request.read_buffer_bytes, config_.read_buffer_bytes);
    const std::size_t write_buffer_bytes =
        SelectSize(request.write_buffer_bytes, config_.write_buffer_bytes);
    const std::size_t socket_receive_buffer_bytes = SelectSize(
        request.socket_receive_buffer_bytes,
        config_.socket_receive_buffer_bytes);
    const std::size_t socket_send_buffer_bytes = SelectSize(
        request.socket_send_buffer_bytes,
        config_.socket_send_buffer_bytes);

    httplib::set_runtime_buffer_sizes(read_buffer_bytes, write_buffer_bytes);
    httplib::Client client(request.endpoint);
    client.set_keep_alive(false);
    client.set_tcp_nodelay(true);
    client.set_socket_options(
        MakeSocketOptions(socket_receive_buffer_bytes, socket_send_buffer_bytes));
    const int timeout_ms =
        request.timeout_ms < 0 ? config_.write_timeout_ms : request.timeout_ms;
    client.set_read_timeout(
        SecondsPart(config_.read_timeout_ms),
        MicrosecondsPart(config_.read_timeout_ms));
    client.set_write_timeout(SecondsPart(timeout_ms), MicrosecondsPart(timeout_ms));
    client.set_payload_max_length(config_.max_payload_bytes);

    httplib::Headers headers = ToHttplibHeaders(request.headers);
    const std::string route = NormalizeRoute(request.route);
    httplib::ContentProvider content_provider =
        [this, provider, &provider_result](
            std::size_t offset,
            std::size_t length,
            httplib::DataSink& sink) {
            if (!provider_result.IsOk()) {
                return false;
            }
            std::vector<char> chunk;
            bool eof = false;
            provider_result = provider(
                static_cast<std::uint64_t>(offset),
                length,
                &chunk,
                &eof);
            if (!provider_result.IsOk()) {
                return false;
            }
            if (!chunk.empty()) {
                if (!sink.write(&chunk[0], chunk.size())) {
                    provider_result = Result<void>(
                        ErrorCode::kIoError,
                        "HTTP upload sink rejected chunk");
                    return false;
                }
                AddBytesSent(chunk.size());
            }
            return true;
        };

    httplib::ContentReceiver response_receiver =
        [&response_result, &on_response_chunk, &response_bytes](
            const char* data,
            std::size_t size) {
            if (!response_result.IsOk()) {
                return false;
            }
            if (on_response_chunk) {
                response_result = on_response_chunk(data, size);
                if (!response_result.IsOk()) {
                    return false;
                }
            }
            response_bytes += size;
            return true;
        };
    httplib::Result result = client.Put(
        route.c_str(),
        headers,
        request.content_length,
        content_provider,
        request.content_type,
        response_receiver);

    info.bytes_transferred = response_bytes;
    if (!result) {
        FinishRequest(false);
        if (!provider_result.IsOk()) {
            return MakeResponseError(
                provider_result.GetError(),
                provider_result.GetMessage());
        }
        if (!response_result.IsOk()) {
            return MakeResponseError(
                response_result.GetError(),
                response_result.GetMessage());
        }
        return MakeResponseError(
            ErrorCode::kIoError,
            httplib::to_string(result.error()));
    }

    info.status_code = result->status;
    info.status_message = httplib::status_message(result->status);
    info.headers = FromHttplibHeaders(result->headers);

    if (result->status < 200 || result->status >= 300) {
        FinishRequest(false);
        return MakeResponseError(
            HttpStatusToErrorCode(result->status),
            BuildHttpStatusMessage(result->status));
    }
    if (!provider_result.IsOk()) {
        FinishRequest(false);
        return MakeResponseError(
            provider_result.GetError(),
            provider_result.GetMessage());
    }
    if (!response_result.IsOk()) {
        FinishRequest(false);
        return MakeResponseError(
            response_result.GetError(),
            response_result.GetMessage());
    }

    FinishRequest(true);
    return Result<HttpResponseInfo>(info);
}

Result<HttpTransferSnapshot> HttpTransportModule::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return Result<HttpTransferSnapshot>(snapshot_);
}

Result<void> HttpTransportModule::OnInit() {
    module_context::framework::IModuleManager* manager = Context().ModuleManager();
    if (manager == NULL) {
        return MakeError(ErrorCode::kInvalidState, "Module manager is unavailable");
    }

    Result<ConfigValue> config = manager->ModuleConfig(ModuleName());
    if (!config.IsOk()) {
        return Result<void>(config.GetError(), config.GetMessage());
    }

    Result<HttpTransportConfig> parsed = ParseConfig(config.Value());
    if (!parsed.IsOk()) {
        return Result<void>(parsed.GetError(), parsed.GetMessage());
    }

    std::lock_guard<std::mutex> lock(mutex_);
    config_ = parsed.Value();
    httplib::set_runtime_buffer_sizes(
        config_.read_buffer_bytes,
        config_.write_buffer_bytes);
    download_handlers_.clear();
    upload_handlers_.clear();
    snapshot_ = HttpTransferSnapshot();
    return foundation::base::MakeSuccess();
}

Result<void> HttpTransportModule::OnStart() {
    if (!ServerRoleEnabled()) {
        return foundation::base::MakeSuccess();
    }

    std::unique_ptr<httplib::Server> server(new httplib::Server());
    server->set_tcp_nodelay(true);
    httplib::set_runtime_buffer_sizes(
        config_.read_buffer_bytes,
        config_.write_buffer_bytes);
    server->set_socket_options(MakeSocketOptions(
        config_.socket_receive_buffer_bytes,
        config_.socket_send_buffer_bytes));
    server->set_keep_alive_max_count(1);
    server->set_keep_alive_timeout(1);
    server->set_read_timeout(
        SecondsPart(config_.read_timeout_ms),
        MicrosecondsPart(config_.read_timeout_ms));
    server->set_write_timeout(
        SecondsPart(config_.write_timeout_ms),
        MicrosecondsPart(config_.write_timeout_ms));
    server->set_payload_max_length(config_.max_payload_bytes);
    const int thread_count = config_.server_thread_count;
    server->new_task_queue = [thread_count]() {
        return new httplib::ThreadPool(static_cast<std::size_t>(thread_count));
    };

    {
        std::lock_guard<std::mutex> lock(mutex_);
        server_.reset(server.release());
    }
    ConfigureServerRoutes();

    server_thread_ = std::thread([this]() {
        bool ok = false;
        httplib::Server* server = NULL;
        std::string listen_address;
        int port = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            server = server_.get();
            listen_address = config_.listen_address;
            port = config_.port;
        }
        if (server != NULL) {
            ok = server->listen(listen_address.c_str(), port);
        }
        if (!ok) {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.failed_requests += 1;
        }
    });

    for (int attempt = 0; attempt < 100; ++attempt) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (server_ && server_->is_running()) {
                return foundation::base::MakeSuccess();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return MakeError(ErrorCode::kTimeout, "HTTP server did not start in time");
}

Result<void> HttpTransportModule::OnStop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (server_) {
            server_->stop();
        }
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    return foundation::base::MakeSuccess();
}

Result<void> HttpTransportModule::OnFini() {
    std::lock_guard<std::mutex> lock(mutex_);
    download_handlers_.clear();
    upload_handlers_.clear();
    server_.reset();
    snapshot_ = HttpTransferSnapshot();
    return foundation::base::MakeSuccess();
}

bool HttpTransportModule::ServerRoleEnabled() const {
    return config_.role == "server" || config_.role == "both";
}

bool HttpTransportModule::ClientRoleEnabled() const {
    return config_.role == "client" || config_.role == "both";
}

void HttpTransportModule::ConfigureServerRoutes() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!server_) {
        return;
    }

    server_->Get(".*", [this](const httplib::Request& req, httplib::Response& res) {
        HandleDownloadRequest(&req, &res);
    });
    httplib::Server::Handler upload_handler =
        [this](const httplib::Request& req, httplib::Response& res) {
            HandleUploadRequest(&req, &res);
        };
    server_->Put(".*", upload_handler);
}

void HttpTransportModule::HandleDownloadRequest(
    const void* raw_request,
    void* raw_response) {
    const httplib::Request& request =
        *static_cast<const httplib::Request*>(raw_request);
    httplib::Response& response = *static_cast<httplib::Response*>(raw_response);

    AddActiveRequest();
    DownloadHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<std::string, DownloadHandler>::const_iterator it =
            download_handlers_.find(request.path);
        if (it != download_handlers_.end()) {
            handler = it->second;
        }
    }

    if (!handler) {
        response.status = 404;
        response.set_content("route not found", "text/plain");
        FinishRequest(false);
        return;
    }

    Result<DownloadResponse> result = handler(MakeServerRequest(request));
    if (!result.IsOk()) {
        response.status = ErrorCodeToHttpStatus(result.GetError());
        response.set_content(result.GetMessage(), "text/plain");
        FinishRequest(false);
        return;
    }

    DownloadResponse download = result.Value();
    if (!download.reader && !download.buffer_reader) {
        response.status = 500;
        response.set_content("download reader is empty", "text/plain");
        FinishRequest(false);
        return;
    }

    response.status = download.status_code;
    HeaderMap::const_iterator header = download.headers.begin();
    for (; header != download.headers.end(); ++header) {
        response.set_header(header->first.c_str(), header->second.c_str());
    }

    response.set_content_provider(
        download.content_length,
        download.content_type.c_str(),
        [this, download](
            std::size_t offset,
            std::size_t length,
            httplib::DataSink& sink) mutable {
            if (download.buffer_reader) {
                const char* data = NULL;
                std::size_t size = 0;
                bool eof = false;
                Result<void> read = download.buffer_reader(
                    static_cast<std::uint64_t>(offset),
                    length,
                    &data,
                    &size,
                    &eof);
                if (!read.IsOk()) {
                    return false;
                }
                if (data != NULL && size > 0) {
                    if (!sink.write(data, size)) {
                        return false;
                    }
                    AddBytesSent(size);
                }
                return true;
            }

            std::vector<char> chunk;
            bool eof = false;
            Result<void> read = download.reader(
                static_cast<std::uint64_t>(offset),
                length,
                &chunk,
                &eof);
            if (!read.IsOk()) {
                return false;
            }
            if (!chunk.empty()) {
                if (!sink.write(&chunk[0], chunk.size())) {
                    return false;
                }
                AddBytesSent(chunk.size());
            }
            return true;
        },
        [this](bool success) {
            FinishRequest(success);
        });
}

void HttpTransportModule::HandleUploadRequest(
    const void* raw_request,
    void* raw_response) {
    const httplib::Request& request =
        *static_cast<const httplib::Request*>(raw_request);
    httplib::Response& response = *static_cast<httplib::Response*>(raw_response);

    AddActiveRequest();
    UploadHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<std::string, UploadHandler>::const_iterator it =
            upload_handlers_.find(request.path);
        if (it != upload_handlers_.end()) {
            handler = it->second;
        }
    }

    if (!handler) {
        response.status = 404;
        response.set_content("route not found", "text/plain");
        FinishRequest(false);
        return;
    }
    if (request.body.size() > config_.max_payload_bytes) {
        response.status = 413;
        response.set_content("payload too large", "text/plain");
        FinishRequest(false);
        return;
    }

    std::vector<char> body(request.body.begin(), request.body.end());
    AddBytesReceived(body.size());
    Result<UploadResponse> result = handler(MakeServerRequest(request), body);
    if (!result.IsOk()) {
        response.status = ErrorCodeToHttpStatus(result.GetError());
        response.set_content(result.GetMessage(), "text/plain");
        FinishRequest(false);
        return;
    }

    UploadResponse upload = result.Value();
    response.status = upload.status_code;
    HeaderMap::const_iterator header = upload.headers.begin();
    for (; header != upload.headers.end(); ++header) {
        response.set_header(header->first.c_str(), header->second.c_str());
    }
    if (!upload.body.empty()) {
        response.set_content(
            &upload.body[0],
            upload.body.size(),
            upload.content_type.c_str());
        AddBytesSent(upload.body.size());
    } else {
        response.set_content("", upload.content_type.c_str());
    }
    FinishRequest(true);
}

void HttpTransportModule::AddActiveRequest() {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.active_requests += 1;
}

void HttpTransportModule::FinishRequest(bool ok) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_.active_requests > 0) {
        snapshot_.active_requests -= 1;
    }
    if (ok) {
        snapshot_.completed_requests += 1;
    } else {
        snapshot_.failed_requests += 1;
    }
}

void HttpTransportModule::AddBytesSent(std::size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.bytes_sent += static_cast<std::uint64_t>(size);
}

void HttpTransportModule::AddBytesReceived(std::size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.bytes_received += static_cast<std::uint64_t>(size);
}

MC_DECLARE_MODULE_FACTORY(HttpTransportModule)

}  // namespace http
}  // namespace module_context
