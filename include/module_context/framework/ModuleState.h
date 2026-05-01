#pragma once

namespace module_context {
namespace framework {

/**
 * @brief 模块在上下文中的生命周期状态。
 *
 * 状态只描述框架视角下的生命周期阶段，不表达业务健康度。模块业务健康度应通过
 * 独立服务接口或诊断接口暴露。
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
