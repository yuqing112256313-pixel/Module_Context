#pragma once

#include <string>
#include <vector>

namespace module_context {
namespace plugin {

/**
 * @brief 单个工序插件的加载配置。
 */
struct PluginEntryConfig {
    std::string name;          ///< 插件实例名，用于通过 GetPlugin<T>(name) 查询。
    std::string type;          ///< 插件类型名，仅用于日志与诊断。
    std::string library_path;  ///< 动态库文件路径（.dll / .so）。
    std::string create_func;   ///< 创建函数导出符号名，例如 "CreatePluginEtching"。
    std::string destroy_func;  ///< 销毁函数导出符号名，例如 "DestroyPluginEtching"。

    PluginEntryConfig()
        : name(),
          type(),
          library_path(),
          create_func(),
          destroy_func() {
    }
};

/**
 * @brief 工序插件管理模块的完整配置。
 */
struct SemipluginManagerConfig {
    std::vector<PluginEntryConfig> plugins;  ///< 需要加载的插件列表，按声明顺序依次初始化。
};

}  // namespace plugin
}  // namespace module_context
