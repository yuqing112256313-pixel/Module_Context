#include "foundation/plugin/DynamicLibrary.h"

#include <string>

#include "foundation/base/ErrorCode.h"
#include "foundation/base/Platform.h"

#if FOUNDATION_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace foundation {
namespace plugin {

namespace {

#if FOUNDATION_PLATFORM_WINDOWS
std::string GetPlatformErrorMessage() {
    DWORD error_code = ::GetLastError();
    if (error_code == 0) {
        return "unknown Windows error";
    }

    LPSTR buffer = NULL;
    DWORD size = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        NULL);

    std::string message;
    if (size > 0 && buffer != NULL) {
        message.assign(buffer, size);
        ::LocalFree(buffer);
    } else {
        message = "unknown Windows error";
    }

    while (!message.empty() &&
           (message[message.size() - 1] == '\r' ||
            message[message.size() - 1] == '\n' ||
            message[message.size() - 1] == ' ')) {
        message.erase(message.size() - 1);
    }

    return message;
}
#else
std::string GetDlErrorMessage() {
    const char* error = ::dlerror();
    return error != NULL ? std::string(error) : std::string("unknown dlerror");
}
#endif

}  // namespace

DynamicLibrary::DynamicLibrary()
    : path_(),
      handle_(NULL) {
}

DynamicLibrary::~DynamicLibrary() {
    (void)Close();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other)
    : path_(other.path_),
      handle_(other.handle_) {
    other.Reset();
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) {
    if (this != &other) {
        (void)Close();
        path_ = other.path_;
        handle_ = other.handle_;
        other.Reset();
    }
    return *this;
}

foundation::base::Result<void> DynamicLibrary::Open(
    const std::string& path) {
    if (path.empty()) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidArgument,
            "DynamicLibrary::Open failed: path is empty");
    }

    if (handle_ != NULL) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidState,
            "DynamicLibrary::Open failed: library already open");
    }

#if FOUNDATION_PLATFORM_WINDOWS
    HMODULE module = ::LoadLibraryA(path.c_str());
    if (module == NULL) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kLibraryLoadError,
            std::string("DynamicLibrary::Open failed: ") +
                GetPlatformErrorMessage());
    }
    handle_ = reinterpret_cast<void*>(module);
#else
    ::dlerror();
    void* module = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (module == NULL) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kLibraryLoadError,
            std::string("DynamicLibrary::Open failed: ") +
                GetDlErrorMessage());
    }
    handle_ = module;
#endif

    path_ = path;
    return foundation::base::Result<void>();
}

foundation::base::Result<void> DynamicLibrary::Close() {
    if (handle_ == NULL) {
        return foundation::base::Result<void>();
    }

#if FOUNDATION_PLATFORM_WINDOWS
    if (!::FreeLibrary(reinterpret_cast<HMODULE>(handle_))) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kOperationFailed,
            std::string("DynamicLibrary::Close failed: ") +
                GetPlatformErrorMessage());
    }
#else
    ::dlerror();
    if (::dlclose(handle_) != 0) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kOperationFailed,
            std::string("DynamicLibrary::Close failed: ") +
                GetDlErrorMessage());
    }
#endif

    Reset();
    return foundation::base::Result<void>();
}

bool DynamicLibrary::IsOpen() const {
    return handle_ != NULL;
}

const std::string& DynamicLibrary::Path() const {
    return path_;
}

foundation::base::Result<void*> DynamicLibrary::GetSymbolRaw(
    const std::string& name) const {
    if (name.empty()) {
        return foundation::base::Result<void*>(
            foundation::base::ErrorCode::kInvalidArgument,
            "DynamicLibrary::GetSymbolRaw failed: symbol name is empty");
    }

    if (handle_ == NULL) {
        return foundation::base::Result<void*>(
            foundation::base::ErrorCode::kInvalidState,
            "DynamicLibrary::GetSymbolRaw failed: library is not open");
    }

#if FOUNDATION_PLATFORM_WINDOWS
    FARPROC proc = ::GetProcAddress(
        reinterpret_cast<HMODULE>(handle_),
        name.c_str());
    if (proc == NULL) {
        return foundation::base::Result<void*>(
            foundation::base::ErrorCode::kSymbolNotFound,
            std::string("DynamicLibrary::GetSymbolRaw failed: ") +
                GetPlatformErrorMessage());
    }
    return foundation::base::Result<void*>(
        reinterpret_cast<void*>(proc));
#else
    ::dlerror();
    void* proc = ::dlsym(handle_, name.c_str());
    const char* error = ::dlerror();
    if (error != NULL) {
        return foundation::base::Result<void*>(
            foundation::base::ErrorCode::kSymbolNotFound,
            std::string("DynamicLibrary::GetSymbolRaw failed: ") + error);
    }
    return foundation::base::Result<void*>(proc);
#endif
}

void DynamicLibrary::Reset() {
    path_.clear();
    handle_ = NULL;
}

}  // namespace plugin
}  // namespace foundation