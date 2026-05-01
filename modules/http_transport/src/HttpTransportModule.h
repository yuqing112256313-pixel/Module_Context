#pragma once

#include "framework/ModuleBase.h"
#include "module_context/http/IHttpTransferService.h"

#include "foundation/base/Result.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace httplib {
class Server;
}

namespace module_context {
namespace http {

struct HttpTransportConfig {
    std::string role;
    std::string listen_address;
    int port;
    std::string endpoint;
    int server_thread_count;
    int read_timeout_ms;
    int write_timeout_ms;
    std::size_t max_payload_bytes;
    std::size_t chunk_bytes;
    std::size_t read_buffer_bytes;
    std::size_t write_buffer_bytes;
    std::size_t socket_receive_buffer_bytes;
    std::size_t socket_send_buffer_bytes;

    HttpTransportConfig()
        : role("client"),
          listen_address("0.0.0.0"),
          port(50080),
          endpoint("http://127.0.0.1:50080"),
          server_thread_count(64),
          read_timeout_ms(30000),
          write_timeout_ms(30000),
          max_payload_bytes(104857600),
          chunk_bytes(8388608),
          read_buffer_bytes(0),
          write_buffer_bytes(0),
          socket_receive_buffer_bytes(0),
          socket_send_buffer_bytes(0) {
    }
};

class HttpTransportModule final
    : public module_context::framework::ModuleBase,
      public IHttpTransferService {
public:
    HttpTransportModule();
    ~HttpTransportModule() override;

    std::string ModuleType() const override;
    std::string ModuleVersion() const override;

    foundation::base::Result<void> RegisterDownloadHandler(
        const std::string& route,
        DownloadHandler handler) override;
    foundation::base::Result<void> RegisterUploadHandler(
        const std::string& route,
        UploadHandler handler) override;
    foundation::base::Result<void> UnregisterHandler(
        const std::string& route) override;
    foundation::base::Result<HttpResponseInfo> Download(
        const HttpDownloadRequest& request,
        HttpChunkHandler on_chunk) override;
    foundation::base::Result<HttpResponseInfo> Upload(
        const HttpUploadRequest& request,
        HttpUploadProvider provider,
        HttpChunkHandler on_response_chunk) override;
    foundation::base::Result<HttpTransferSnapshot> GetSnapshot() const override;

protected:
    foundation::base::Result<void> OnInit() override;
    foundation::base::Result<void> OnStart() override;
    foundation::base::Result<void> OnStop() override;
    foundation::base::Result<void> OnFini() override;

private:
    bool ServerRoleEnabled() const;
    bool ClientRoleEnabled() const;
    void ConfigureServerRoutes();
    void HandleDownloadRequest(const void* raw_request, void* raw_response);
    void HandleUploadRequest(const void* raw_request, void* raw_response);
    void AddActiveRequest();
    void FinishRequest(bool ok);
    void AddBytesSent(std::size_t size);
    void AddBytesReceived(std::size_t size);

private:
    mutable std::mutex mutex_;
    HttpTransportConfig config_;
    std::map<std::string, DownloadHandler> download_handlers_;
    std::map<std::string, UploadHandler> upload_handlers_;
    HttpTransferSnapshot snapshot_;
    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;
};

}  // namespace http
}  // namespace module_context
