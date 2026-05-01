#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "PluginLoader.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/log/Logger.h"

#include <string>

// 工序插件基类，由 SEMIPLUGIN 层提供。
// 约定：IPlugin 须为多态类（至少含虚析构函数），以支持 GetPlugin<T> 的 dynamic_cast。
#include "iplugin.h"

namespace module_context {
namespace plugin {

PluginLoader::PluginLoader() {
}

PluginLoader::~PluginLoader() {
}

std::string PluginLoader::GetLastLibraryError() {
#if defined(_WIN32)
    DWORD error_code = GetLastError();
    if (error_code == 0) {
        return "unknown error";
    }

    char* message_buffer = NULL;
    DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&message_buffer),
        0,
        NULL);

    std::string result =
        (size > 0 && message_buffer != NULL) ? std::string(message_buffer, size) : "unknown error";
    if (message_buffer != NULL) {
        LocalFree(message_buffer);
    }
    return result;
#else
    const char* error = dlerror();
    return error != NULL ? error : "unknown error";
#endif
}

foundation::base::Result<void*> PluginLoader::OpenLibrary(const std::string& path) {
    if (path.empty()) {
        return foundation::base::Result<void*>(
            foundation::base::ErrorCode::kInvalidArgument,
            "Library path must not be empty");
    }

#if defined(_WIN32)
    void* handle = static_cast<void*>(LoadLibraryA(path.c_str()));
#else
    void* handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif

    if (handle == NULL) {
        return foundation::base::Result<void*>(
            foundation::base::ErrorCode::kIoError,
            "Failed to load library '" + path + "': " + GetLastLibraryError());
    }

    return foundation::base::Result<void*>(handle);
}

void PluginLoader::CloseLibrary(void* handle) {
    if (handle == NULL) {
        return;
    }

#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

void* PluginLoader::GetSymbol(void* handle, const std::string& symbol) {
    if (handle == NULL || symbol.empty()) {
        return NULL;
    }

#if defined(_WIN32)
    return reinterpret_cast<void*>(
        GetProcAddress(static_cast<HMODULE>(handle), symbol.c_str()));
#else
    return dlsym(handle, symbol.c_str());
#endif
}

foundation::base::Result<LoadedPlugin> PluginLoader::Load(const PluginEntryConfig& config) {
    if (config.name.empty()) {
        return foundation::base::Result<LoadedPlugin>(
            foundation::base::ErrorCode::kInvalidArgument,
            "Plugin name must not be empty");
    }

    if (config.library_path.empty()) {
        return foundation::base::Result<LoadedPlugin>(
            foundation::base::ErrorCode::kInvalidArgument,
            "Plugin library path must not be empty for '" + config.name + "'");
    }

    if (config.create_func.empty() || config.destroy_func.empty()) {
        return foundation::base::Result<LoadedPlugin>(
            foundation::base::ErrorCode::kInvalidArgument,
            "Plugin create/destroy function names must not be empty for '" +
                config.name + "'");
    }

    foundation::base::Result<void*> open_result = OpenLibrary(config.library_path);
    if (!open_result.IsOk()) {
        return foundation::base::Result<LoadedPlugin>(
            open_result.GetError(),
            open_result.GetMessage());
    }

    void* handle = open_result.Value();

    typedef Hh::Api::Plugin::IPlugin* (*CreateFunc)();
    typedef void (*DestroyFunc)(Hh::Api::Plugin::IPlugin*);

    CreateFunc create_func = reinterpret_cast<CreateFunc>(
        GetSymbol(handle, config.create_func));
    if (create_func == NULL) {
        CloseLibrary(handle);
        return foundation::base::Result<LoadedPlugin>(
            foundation::base::ErrorCode::kNotFound,
            "Symbol '" + config.create_func + "' not found in '" +
                config.library_path + "'");
    }

    DestroyFunc destroy_func = reinterpret_cast<DestroyFunc>(
        GetSymbol(handle, config.destroy_func));
    if (destroy_func == NULL) {
        CloseLibrary(handle);
        return foundation::base::Result<LoadedPlugin>(
            foundation::base::ErrorCode::kNotFound,
            "Symbol '" + config.destroy_func + "' not found in '" +
                config.library_path + "'");
    }

    Hh::Api::Plugin::IPlugin* instance = NULL;
    try {
        instance = create_func();
    } catch (...) {
        CloseLibrary(handle);
        return foundation::base::Result<LoadedPlugin>(
            foundation::base::ErrorCode::kOperationFailed,
            "Exception thrown by create function '" + config.create_func +
                "' for plugin '" + config.name + "'");
    }

    if (instance == NULL) {
        CloseLibrary(handle);
        return foundation::base::Result<LoadedPlugin>(
            foundation::base::ErrorCode::kOperationFailed,
            "Create function '" + config.create_func +
                "' returned null for plugin '" + config.name + "'");
    }

    LoadedPlugin plugin;
    plugin.library_handle = handle;
    plugin.destroy_func = destroy_func;
    plugin.instance = instance;
    return foundation::base::Result<LoadedPlugin>(plugin);
}

void PluginLoader::Unload(LoadedPlugin* plugin) {
    if (plugin == NULL) {
        return;
    }

    if (plugin->instance != NULL && plugin->destroy_func != NULL) {
        try {
            plugin->destroy_func(plugin->instance);
        } catch (...) {
            FOUNDATION_LOG_ERROR(
                "[PluginLoader] Exception thrown by plugin destroy function");
        }
        plugin->instance = NULL;
    }

    if (plugin->library_handle != NULL) {
        CloseLibrary(plugin->library_handle);
        plugin->library_handle = NULL;
    }

    plugin->destroy_func = NULL;
}

}  // namespace plugin
}  // namespace module_context
