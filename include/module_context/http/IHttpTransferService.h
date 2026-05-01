#pragma once

#include "module_context/framework/Export.h"
#include "module_context/http/Types.h"

#include "foundation/base/Result.h"

#include <string>

namespace module_context {
namespace http {

class MC_FRAMEWORK_API IHttpTransferService {
public:
    virtual ~IHttpTransferService() {}

    virtual foundation::base::Result<void> RegisterDownloadHandler(
        const std::string& route,
        DownloadHandler handler) = 0;

    virtual foundation::base::Result<void> RegisterUploadHandler(
        const std::string& route,
        UploadHandler handler) = 0;

    virtual foundation::base::Result<void> UnregisterHandler(
        const std::string& route) = 0;

    virtual foundation::base::Result<HttpResponseInfo> Download(
        const HttpDownloadRequest& request,
        HttpChunkHandler on_chunk) = 0;

    virtual foundation::base::Result<HttpResponseInfo> Upload(
        const HttpUploadRequest& request,
        HttpUploadProvider provider,
        HttpChunkHandler on_response_chunk) = 0;

    virtual foundation::base::Result<HttpTransferSnapshot> GetSnapshot() const = 0;
};

}  // namespace http
}  // namespace module_context

namespace module_context {
namespace framework {

template <typename T>
struct ServiceTypeTraits;

template <>
struct ServiceTypeTraits<module_context::http::IHttpTransferService> {
    static const char* Key() {
        return "module_context.http.IHttpTransferService";
    }
};

}  // namespace framework
}  // namespace module_context
