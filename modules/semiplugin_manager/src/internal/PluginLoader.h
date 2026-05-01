#pragma once

#include "ConfigTypes.h"

#include "foundation/base/NonCopyable.h"
#include "foundation/base/Result.h"

namespace Hh {
namespace Api {
namespace Plugin {
class IPlugin;
}  // namespace Plugin
}  // namespace Api
}  // namespace Hh

namespace module_context {
namespace plugin {

/**
 * @brief 已装载的插件运行时记录，持有 DLL 句柄和插件实例指针。
 *
 * Unload() 负责安全地释放二者；字段不得在 Unload 调用后继续使用。
 */
struct LoadedPlugin {
    void* library_handle;                            //系统句柄
    void (*destroy_func)(Hh::Api::Plugin::IPlugin*); //专属销毁函数指针
    Hh::Api::Plugin::IPlugin* instance;              //业务实例指针

    LoadedPlugin()
        : library_handle(NULL),
          destroy_func(NULL),
          instance(NULL) {
    }
};

/**
 * @brief 工序插件 DLL 动态加载器。
 *
 * 封装平台相关的动态库加载/符号查找/卸载操作（Windows: LoadLibrary /
 * GetProcAddress / FreeLibrary；Linux: dlopen / dlsym / dlclose）。
 *
 * 约定：插件 DLL 须导出以下两个 C 符号（符号名可通过配置自定义）：
 *   - `Hh::Api::Plugin::IPlugin* CreateXxx()`
 *   - `void DestroyXxx(Hh::Api::Plugin::IPlugin*)`
 */
class PluginLoader : private foundation::base::NonCopyable {
public:
    PluginLoader();
    ~PluginLoader();

    /**
     * @brief 根据配置加载 DLL 并创建插件实例。
     *
     * @param config 插件加载配置。
     * @return 成功时返回 LoadedPlugin 记录；失败时返回详细错误。
     */
    foundation::base::Result<LoadedPlugin> Load(const PluginEntryConfig& config);

    /**
     * @brief 安全卸载插件：调用销毁函数并释放 DLL 句柄。
     *
     * @param plugin 待卸载的插件记录；调用后内部指针均置 NULL。
     */
    void Unload(LoadedPlugin* plugin);

private:
    static foundation::base::Result<void*> OpenLibrary(const std::string& path);
    static void CloseLibrary(void* handle);
    static void* GetSymbol(void* handle, const std::string& symbol);
    static std::string GetLastLibraryError();
};

}  // namespace plugin
}  // namespace module_context
