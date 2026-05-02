#pragma once

#include "module_context/framework/Export.h"
#include "module_context/framework/IModule.h"

namespace module_context {
namespace framework {

/**
 * @brief 模块插件 ABI 版本号。
 *
 * 模块插件通过固定 C 符号与宿主进程交互。宿主加载动态库时会先读取该版本号，
 * 只有版本一致才继续解析创建/销毁入口，避免不同 ABI 约定的插件被误加载。
 */
static const int kModulePluginApiVersion = 2;

/**
 * @brief 声明模块插件的 C ABI 导出符号，并指定自定义 ABI 版本。
 *
 * 该宏应放在插件实现文件末尾。它导出三类固定符号：
 * `GetPluginApiVersion()`、`CreatePlugin()` 和 `DestroyPlugin()`。宿主侧
 * `ModuleManager` 只依赖这些符号，不需要链接或认识具体模块类型。
 *
 * @param ModuleType 具体模块类型，需实现 `IModule`，并可被默认构造。
 * @param ApiVersion 插件 ABI 版本号，通常使用框架默认版本。
 *
 * @note 插件对象由插件动态库内创建，也必须由同一动态库内的 `DestroyPlugin`
 *       销毁，避免跨运行库或跨动态库释放内存带来的兼容性问题。
 */
#define MC_DECLARE_MODULE_FACTORY_WITH_API_VERSION(ModuleType, ApiVersion)          \
    extern "C" MC_PLUGIN_EXPORT int GetPluginApiVersion() {                         \
        return (ApiVersion);                                                        \
    }                                                                               \
    extern "C" MC_PLUGIN_EXPORT ::module_context::framework::IModule*               \
        CreatePlugin() {                                                            \
        return new ModuleType();                                                    \
    }                                                                               \
    extern "C" MC_PLUGIN_EXPORT void DestroyPlugin(                                 \
        ::module_context::framework::IModule* module) {                             \
        delete module;                                                              \
    }

/**
 * @brief 声明模块插件的 C ABI 导出符号，并使用框架默认 ABI 版本。
 *
 * 绝大多数模块插件应使用该宏；只有在明确维护多套 ABI 兼容层时，才使用
 * `MC_DECLARE_MODULE_FACTORY_WITH_API_VERSION`。
 */
#define MC_DECLARE_MODULE_FACTORY(ModuleType)                                       \
    MC_DECLARE_MODULE_FACTORY_WITH_API_VERSION(                                     \
        ModuleType,                                                                 \
        ::module_context::framework::kModulePluginApiVersion)

}  // namespace framework
}  // namespace module_context
