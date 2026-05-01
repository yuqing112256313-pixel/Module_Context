#pragma once

#include "module_context/framework/IContext.h"
#include "module_context/framework/IModule.h"

#include "foundation/base/NonCopyable.h"
#include "foundation/base/Result.h"

namespace module_context {
namespace framework {

/**
 * @brief 模块实现的生命周期基类。
 *
 * `ModuleBase` 固化通用状态机，并把业务扩展点收敛到 `OnInit()`、`OnStart()`、
 * `OnStop()` 和 `OnFini()`。具体模块只需要实现元信息和必要的钩子函数，避免每个
 * 模块重复编写状态校验、错误包装和上下文保存逻辑。
 */
class MC_FRAMEWORK_API ModuleBase
    : public virtual IModule,
      private foundation::base::NonCopyable {
public:
    ModuleBase();
    ~ModuleBase() override;

    std::string ModuleName() const override;
    foundation::base::Result<void> SetModuleName(
        const std::string& name) override;
    ModuleState State() const override;

    foundation::base::Result<void> Init(IContext& ctx) override final;
    foundation::base::Result<void> Start() override final;
    foundation::base::Result<void> Stop() override final;
    foundation::base::Result<void> Fini() override final;

protected:
    /**
     * @brief 获取所属上下文。
     *
     * @return `Init()` 成功注入的上下文引用。
     *
     * @note 只能在 `Init()` 之后、`Fini()` 之前调用。
     */
    IContext& Context() const;
    /**
     * @brief 判断当前模块是否已有上下文。
     *
     * @return 已注入上下文返回 `true`，否则返回 `false`。
     */
    bool HasContext() const;

    /**
     * @brief 初始化钩子。
     *
     * 子类在这里读取配置、解析依赖和准备资源。默认实现为空操作。
     */
    virtual foundation::base::Result<void> OnInit();
    /**
     * @brief 启动钩子。
     *
     * 子类在这里启动运行态行为，例如线程、网络连接或消息订阅。默认实现为空操作。
     */
    virtual foundation::base::Result<void> OnStart();
    /**
     * @brief 停止钩子。
     *
     * 子类在这里停止运行态行为，并应尽量做到失败可诊断。默认实现为空操作。
     */
    virtual foundation::base::Result<void> OnStop();
    /**
     * @brief 反初始化钩子。
     *
     * 子类在这里释放 `OnInit()` 阶段建立的资源。默认实现为空操作。
     */
    virtual foundation::base::Result<void> OnFini();

private:
    static bool IsValidTransition(ModuleState from, ModuleState to);

private:
    IContext* ctx_;
    std::string module_name_;
    ModuleState state_;
};

}  // namespace framework
}  // namespace module_context
