#include "HttpTransportModule.h"

#include "module_context/framework/IContext.h"
#include "module_context/framework/IModuleManager.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/config/ConfigValue.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using foundation::base::ErrorCode;
using foundation::config::ConfigValue;
using module_context::http::DownloadResponse;
using module_context::http::HttpDownloadRequest;
using module_context::http::HttpResponseInfo;
using module_context::http::HttpServerRequest;
using module_context::http::HttpTransportModule;
using module_context::http::HttpUploadRequest;
using module_context::http::UploadResponse;

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

ConfigValue MakeHttpConfig(int port) {
    ConfigValue root = ConfigValue::MakeObject();
    SetField(&root, "role", ConfigValue("both"));
    SetField(&root, "listen_address", ConfigValue("127.0.0.1"));
    SetField(&root, "port", ConfigValue(static_cast<std::int64_t>(port)));
    SetField(
        &root,
        "endpoint",
        ConfigValue(std::string("http://127.0.0.1:") + std::to_string(port)));
    SetField(&root, "server_thread_count", ConfigValue(static_cast<std::int64_t>(2)));
    SetField(&root, "read_timeout_ms", ConfigValue(static_cast<std::int64_t>(3000)));
    SetField(&root, "write_timeout_ms", ConfigValue(static_cast<std::int64_t>(3000)));
    SetField(&root, "max_payload_bytes", ConfigValue(static_cast<std::int64_t>(134217728)));
    SetField(&root, "chunk_bytes", ConfigValue(static_cast<std::int64_t>(4)));
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
        if (name != "http_transport") {
            return foundation::base::Result<ConfigValue>(
                ErrorCode::kNotFound,
                "Unknown module config");
        }
        return foundation::base::Result<ConfigValue>(module_config_);
    }

private:
    foundation::base::Result<module_context::framework::IModule*> LookupModuleRaw(
        const std::string&) override {
        return foundation::base::Result<module_context::framework::IModule*>(
            ErrorCode::kNotFound,
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
            ErrorCode::kNotFound,
            "No services are registered");
    }

    foundation::base::Result<module_context::framework::IModule*> LookupUniqueServiceRaw(
        const char*) override {
        return foundation::base::Result<module_context::framework::IModule*>(
            ErrorCode::kNotFound,
            "No services are registered");
    }

    module_context::framework::IModuleManager* manager_;
};

foundation::base::Result<DownloadResponse> MakeDownload(
    const std::vector<char>& payload,
    const HttpServerRequest& request) {
    if (request.headers.find("X-Test-Token") == request.headers.end() ||
        request.headers.find("X-Test-Token")->second != "ok") {
        return foundation::base::Result<DownloadResponse>(
            ErrorCode::kPermissionDenied,
            "bad token");
    }

    DownloadResponse response;
    response.content_length = payload.size();
    response.headers["X-Test-Reply"] = "stream";
    response.reader =
        [payload](
            std::uint64_t offset,
            std::size_t max_bytes,
            std::vector<char>* chunk,
            bool* eof) -> foundation::base::Result<void> {
        if (chunk == NULL || eof == NULL) {
            return foundation::base::Result<void>(
                ErrorCode::kInvalidArgument,
                "invalid chunk output");
        }
        if (offset >= payload.size()) {
            *eof = true;
            return foundation::base::MakeSuccess();
        }
        const std::size_t begin = static_cast<std::size_t>(offset);
        const std::size_t count =
            std::min<std::size_t>(max_bytes, payload.size() - begin);
        chunk->assign(payload.begin() + begin, payload.begin() + begin + count);
        *eof = (begin + count) >= payload.size();
        return foundation::base::MakeSuccess();
    };
    return foundation::base::Result<DownloadResponse>(response);
}

std::vector<char> MakePatternPayload(std::size_t size) {
    std::vector<char> payload(size);
    for (std::size_t index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<char>((index * 131u + 17u) & 0xffu);
    }
    return payload;
}

std::uint64_t HashPayload(const std::vector<char>& payload) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < payload.size(); ++index) {
        hash ^= static_cast<unsigned char>(payload[index]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

foundation::base::Result<DownloadResponse> MakeBufferDownload(
    const std::shared_ptr<const std::vector<char> >& payload) {
    DownloadResponse response;
    response.content_length = payload ? payload->size() : 0;
    response.buffer_reader =
        [payload](
            std::uint64_t offset,
            std::size_t max_bytes,
            const char** data,
            std::size_t* size,
            bool* eof) -> foundation::base::Result<void> {
        if (!payload || data == NULL || size == NULL || eof == NULL) {
            return foundation::base::Result<void>(
                ErrorCode::kInvalidArgument,
                "invalid buffer output");
        }
        if (offset >= payload->size()) {
            *data = NULL;
            *size = 0;
            *eof = true;
            return foundation::base::MakeSuccess();
        }
        const std::size_t begin = static_cast<std::size_t>(offset);
        const std::size_t count =
            std::min<std::size_t>(max_bytes, payload->size() - begin);
        *data = &(*payload)[begin];
        *size = count;
        *eof = (begin + count) >= payload->size();
        return foundation::base::MakeSuccess();
    };
    return foundation::base::Result<DownloadResponse>(response);
}

bool RunHttpCase() {
    const int port = 55180;
    DummyModuleManager manager(MakeHttpConfig(port));
    DummyContext context(&manager);
    HttpTransportModule module;

    if (!Expect(module.Init(context).IsOk(), "HTTP module Init should succeed")) {
        return false;
    }

    foundation::base::Result<void> empty_route =
        module.RegisterDownloadHandler("", [](const HttpServerRequest&) {
            return foundation::base::Result<DownloadResponse>(DownloadResponse());
        });
    if (!Expect(
            !empty_route.IsOk() && empty_route.GetError() == ErrorCode::kInvalidArgument,
            "HTTP module should reject empty routes")) {
        return false;
    }

    const std::vector<char> payload = {'a', 'b', 'c', 'd', 'e', 'f', 'g'};
    foundation::base::Result<void> register_download =
        module.RegisterDownloadHandler(
            "/download",
            [&payload](const HttpServerRequest& request) {
                return MakeDownload(payload, request);
            });
    if (!Expect(register_download.IsOk(), "Download handler should register")) {
        return false;
    }

    foundation::base::Result<void> duplicate =
        module.RegisterDownloadHandler(
            "/download",
            [](const HttpServerRequest&) {
                return foundation::base::Result<DownloadResponse>(DownloadResponse());
            });
    if (!Expect(
            !duplicate.IsOk() && duplicate.GetError() == ErrorCode::kAlreadyExists,
            "HTTP module should reject duplicate routes")) {
        return false;
    }

    foundation::base::Result<void> register_upload =
        module.RegisterUploadHandler(
            "/upload",
            [](const HttpServerRequest&, const std::vector<char>& body) {
                UploadResponse response;
                response.content_type = "text/plain";
                response.body.assign(body.begin(), body.end());
                return foundation::base::Result<UploadResponse>(response);
            });
    if (!Expect(register_upload.IsOk(), "Upload handler should register")) {
        return false;
    }

    std::shared_ptr<const std::vector<char> > large_payload(
        new std::vector<char>(MakePatternPayload(20u * 1024u * 1024u)));
    foundation::base::Result<void> register_large_download =
        module.RegisterDownloadHandler(
            "/download-large",
            [large_payload](const HttpServerRequest&) {
                return MakeBufferDownload(large_payload);
            });
    if (!Expect(
            register_large_download.IsOk(),
            "Large buffer download handler should register")) {
        return false;
    }

    if (!Expect(module.Start().IsOk(), "HTTP module Start should succeed")) {
        return false;
    }

    HttpDownloadRequest request;
    request.endpoint = std::string("http://127.0.0.1:") + std::to_string(port);
    request.route = "/download";
    request.headers["X-Test-Token"] = "ok";
    request.timeout_ms = 3000;
    std::vector<char> downloaded;
    foundation::base::Result<HttpResponseInfo> download_result =
        module.Download(
            request,
            [&downloaded](const char* data, std::size_t size) {
                downloaded.insert(downloaded.end(), data, data + size);
                return foundation::base::MakeSuccess();
            });
    if (!Expect(download_result.IsOk(), "Download should succeed")) {
        return false;
    }
    if (!Expect(downloaded == payload, "Downloaded chunks should preserve order")) {
        return false;
    }
    if (!Expect(
            download_result.Value().headers["X-Test-Reply"] == "stream",
            "Download should expose response headers")) {
        return false;
    }

    HttpDownloadRequest large_request = request;
    large_request.route = "/download-large";
    large_request.max_bytes = large_payload->size() + 1;
    std::vector<char> large_downloaded;
    large_downloaded.resize(large_payload->size());
    std::size_t large_received = 0;
    foundation::base::Result<HttpResponseInfo> large_download_result =
        module.Download(
            large_request,
            [&large_downloaded, &large_received](
                const char* data,
                std::size_t size) -> foundation::base::Result<void> {
                if (large_received + size > large_downloaded.size()) {
                    return foundation::base::Result<void>(
                        ErrorCode::kOutOfRange,
                        "large download exceeded expected size");
                }
                std::copy(data, data + size, large_downloaded.begin() + large_received);
                large_received += size;
                return foundation::base::MakeSuccess();
            });
    if (!Expect(large_download_result.IsOk(), "Large buffer download should succeed")) {
        return false;
    }
    if (!Expect(
            large_received == large_payload->size() &&
                HashPayload(large_downloaded) == HashPayload(*large_payload),
            "Large buffer download should preserve byte count and hash")) {
        return false;
    }

    HttpDownloadRequest missing = request;
    missing.route = "/missing";
    foundation::base::Result<HttpResponseInfo> missing_result =
        module.Download(
            missing,
            [](const char*, std::size_t) { return foundation::base::MakeSuccess(); });
    if (!Expect(
            !missing_result.IsOk() && missing_result.GetError() == ErrorCode::kNotFound,
            "Missing download route should return kNotFound")) {
        return false;
    }

    HttpUploadRequest upload;
    upload.endpoint = request.endpoint;
    upload.route = "/upload";
    upload.content_length = 3;
    upload.timeout_ms = 3000;
    const std::vector<char> upload_payload = {'x', 'y', 'z'};
    std::vector<char> upload_response;
    foundation::base::Result<HttpResponseInfo> upload_result =
        module.Upload(
            upload,
            [&upload_payload](
                std::uint64_t offset,
                std::size_t max_bytes,
                std::vector<char>* chunk,
                bool* eof) -> foundation::base::Result<void> {
                if (offset >= upload_payload.size()) {
                    *eof = true;
                    return foundation::base::MakeSuccess();
                }
                const std::size_t begin = static_cast<std::size_t>(offset);
                const std::size_t count =
                    std::min<std::size_t>(max_bytes, upload_payload.size() - begin);
                chunk->assign(
                    upload_payload.begin() + begin,
                    upload_payload.begin() + begin + count);
                *eof = (begin + count) >= upload_payload.size();
                return foundation::base::MakeSuccess();
            },
            [&upload_response](const char* data, std::size_t size) {
                upload_response.insert(upload_response.end(), data, data + size);
                return foundation::base::MakeSuccess();
            });
    if (!Expect(upload_result.IsOk(), "Upload should succeed")) {
        return false;
    }
    if (!Expect(upload_response == upload_payload, "Upload response should echo body")) {
        return false;
    }

    if (!Expect(module.Stop().IsOk(), "HTTP module Stop should succeed")) {
        return false;
    }
    if (!Expect(module.Fini().IsOk(), "HTTP module Fini should succeed")) {
        return false;
    }

    return true;
}

}  // namespace

int main() {
    if (!RunHttpCase()) {
        return 1;
    }

    std::cout << "[PASSED] http_transport_module_test" << std::endl;
    return 0;
}
