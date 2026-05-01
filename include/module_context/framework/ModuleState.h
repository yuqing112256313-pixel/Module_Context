#pragma once

namespace module_context {
namespace framework {

/**
 * @brief 模块在上下文中的生命周期状态。
 */
enum class ModuleState {
    Created = 0,  ///< 模块对象已构造，但尚未执行 `Init()`。
    Inited,       ///< `Init()` 成功，模块资源已准备完成。
    Started,      ///< `Start()` 成功，模块业务处于运行状态。
    Stopped,      ///< `Stop()` 成功，运行已停止，可再次 `Start()`。
    Fini          ///< `Fini()` 完成，模块资源已释放。
};

}  // namespace framework
}  // namespace module_context
