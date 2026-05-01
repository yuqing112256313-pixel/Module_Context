#pragma once

#include "module_context/framework/Export.h"
#include "module_context/framework/IModule.h"

namespace module_context {
namespace framework {

/// 插件 ABI 版本号，宿主在加载插件时会校验一致性。
static const int kModulePluginApiVersion = 2;

// 该头文件只提供最小 C ABI 导出封装。
// 插件通过固定符号跨动态库边界暴露创建/销毁入口，不复用进程内注册型工厂。

/**
 * @brief 声明插件工厂导出符号，并指定自定义 ABI 版本。
 *
 * @param ModuleType 具体模块类型，需实现 IModule。
 * @param ApiVersion 插件 ABI 版本号。
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
 * @brief 声明插件工厂导出符号，并使用默认 ABI 版本。
 */
#define MC_DECLARE_MODULE_FACTORY(ModuleType)                                       \
    MC_DECLARE_MODULE_FACTORY_WITH_API_VERSION(                                     \
        ModuleType,                                                                 \
        ::module_context::framework::kModulePluginApiVersion)

}  // namespace framework
}  // namespace module_context
