#pragma once

#include "module_context/framework/Export.h"

#include "foundation/base/Result.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace module_context {
namespace http {

typedef std::map<std::string, std::string> HeaderMap;

struct MC_FRAMEWORK_API HttpServerRequest {
    std::string route;
    HeaderMap headers;
    std::string remote_address;

    HttpServerRequest()
        : route(),
          headers(),
          remote_address() {
    }
};

typedef std::function<foundation::base::Result<void>(
    std::uint64_t offset,
    std::size_t max_bytes,
    std::vector<char>* chunk,
    bool* eof)> HttpReadCallback;

typedef std::function<foundation::base::Result<void>(
    std::uint64_t offset,
    std::size_t max_bytes,
    const char** data,
    std::size_t* size,
    bool* eof)> HttpBufferReadCallback;

struct MC_FRAMEWORK_API DownloadResponse {
    int status_code;
    std::string content_type;
    HeaderMap headers;
    std::size_t content_length;
    HttpReadCallback reader;
    HttpBufferReadCallback buffer_reader;

    DownloadResponse()
        : status_code(200),
          content_type("application/octet-stream"),
          headers(),
          content_length(0),
          reader(),
          buffer_reader() {
    }
};

struct MC_FRAMEWORK_API UploadResponse {
    int status_code;
    std::string content_type;
    HeaderMap headers;
    std::vector<char> body;

    UploadResponse()
        : status_code(200),
          content_type("text/plain"),
          headers(),
          body() {
    }
};

struct MC_FRAMEWORK_API HttpDownloadRequest {
    std::string endpoint;
    std::string route;
    HeaderMap headers;
    int timeout_ms;
    std::size_t max_bytes;
    std::size_t read_buffer_bytes;
    std::size_t write_buffer_bytes;
    std::size_t socket_receive_buffer_bytes;
    std::size_t socket_send_buffer_bytes;

    HttpDownloadRequest()
        : endpoint(),
          route(),
          headers(),
          timeout_ms(30000),
          max_bytes(0),
          read_buffer_bytes(0),
          write_buffer_bytes(0),
          socket_receive_buffer_bytes(0),
          socket_send_buffer_bytes(0) {
    }
};

typedef std::function<foundation::base::Result<void>(
    const char* data,
    std::size_t size)> HttpChunkHandler;

typedef std::function<foundation::base::Result<void>(
    std::uint64_t offset,
    std::size_t max_bytes,
    std::vector<char>* chunk,
    bool* eof)> HttpUploadProvider;

struct MC_FRAMEWORK_API HttpUploadRequest {
    std::string endpoint;
    std::string route;
    HeaderMap headers;
    std::string content_type;
    std::size_t content_length;
    int timeout_ms;
    std::size_t read_buffer_bytes;
    std::size_t write_buffer_bytes;
    std::size_t socket_receive_buffer_bytes;
    std::size_t socket_send_buffer_bytes;

    HttpUploadRequest()
        : endpoint(),
          route(),
          headers(),
          content_type("application/octet-stream"),
          content_length(0),
          timeout_ms(30000),
          read_buffer_bytes(0),
          write_buffer_bytes(0),
          socket_receive_buffer_bytes(0),
          socket_send_buffer_bytes(0) {
    }
};

struct MC_FRAMEWORK_API HttpResponseInfo {
    int status_code;
    std::string status_message;
    HeaderMap headers;
    std::size_t bytes_transferred;
    double setup_ms;
    double header_wait_ms;
    double first_byte_ms;
    double body_ms;
    double chunk_callback_ms;
    double total_ms;
    std::size_t chunk_count;

    HttpResponseInfo()
        : status_code(0),
          status_message(),
          headers(),
          bytes_transferred(0),
          setup_ms(0.0),
          header_wait_ms(0.0),
          first_byte_ms(0.0),
          body_ms(0.0),
          chunk_callback_ms(0.0),
          total_ms(0.0),
          chunk_count(0) {
    }
};

struct MC_FRAMEWORK_API HttpTransferSnapshot {
    std::uint64_t active_requests;
    std::uint64_t completed_requests;
    std::uint64_t failed_requests;
    std::uint64_t bytes_sent;
    std::uint64_t bytes_received;

    HttpTransferSnapshot()
        : active_requests(0),
          completed_requests(0),
          failed_requests(0),
          bytes_sent(0),
          bytes_received(0) {
    }
};

typedef std::function<foundation::base::Result<DownloadResponse>(
    const HttpServerRequest& request)> DownloadHandler;

typedef std::function<foundation::base::Result<UploadResponse>(
    const HttpServerRequest& request,
    const std::vector<char>& body)> UploadHandler;

}  // namespace http
}  // namespace module_context
