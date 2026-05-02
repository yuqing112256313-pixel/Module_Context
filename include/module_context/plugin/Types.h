#pragma once

namespace module_context {
namespace plugin {

/**
 * @brief 工序插件生命周期状态枚举。
 *
 * 该状态由插件管理模块对其下游业务/算法插件进行维护，和框架模块自身的
 * `ModuleState` 分属两层生命周期：前者描述被管理插件，后者描述框架模块。
 */
enum class PluginState {
    kUnloaded    = 0, ///< 插件尚未加载或已被卸载。
    kLoaded      = 1, ///< 动态库已装载，实例已创建，Init 尚未调用。
    kInitialized = 2, ///< Init() 已成功完成。
    kStarted     = 3, ///< Start() 已成功完成，业务运行中。
    kStopped     = 4, ///< Stop() 已成功完成。
    kError       = 5, ///< 任一生命周期函数失败，插件不可用。
};

}  // namespace plugin
}  // namespace module_context
