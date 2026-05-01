#pragma once

#include "module_context/framework/Export.h"
#include "module_context/framework/ModuleState.h"

#include "foundation/base/Result.h"

#include <string>

namespace module_context {
namespace framework {

class IContext;

/**
 * @brief 模块运行时对象的公开抽象接口。
 *
 * 模块实例由模块管理器创建并驱动完整生命周期。外部使用者通常只通过
 * `IModuleManager::Module<T>()` 获取模块实例元信息，不直接依赖具体实现类。
 */
class MC_FRAMEWORK_API IModule {
public:
    /**
     * @brief 析构模块接口。
     */
    virtual ~IModule() {}

    /**
     * @brief 返回模块实例名。
     *
     * @return 配置中声明的模块实例名；若尚未注入实例名，可退化为模块类型名。
     */
    virtual std::string ModuleName() const = 0;
    /**
     * @brief 返回模块类型名。
     *
     * @return 稳定的逻辑类型名，用于和配置中的 `type` 字段对齐。
     */
    virtual std::string ModuleType() const = 0;
    /**
     * @brief 返回模块版本号。
     *
     * @return 语义清晰的版本字符串，例如 `1.0.0`。
     */
    virtual std::string ModuleVersion() const = 0;
    /**
     * @brief 注入模块实例名。
     *
     * @param name 配置中的模块实例名。
     * @return 成功返回 `Ok`；若名称为空或生命周期状态不允许修改，则返回错误。
     *
     * @note 该方法由框架在进入生命周期前调用，业务代码通常不需要直接调用。
     */
    virtual foundation::base::Result<void> SetModuleName(
        const std::string& name) = 0;

    /**
     * @brief 初始化模块资源。
     *
     * @param ctx 所属上下文。
     * @return 成功返回 `Ok`；失败时返回初始化错误。
     */
    virtual foundation::base::Result<void> Init(IContext& ctx) = 0;
    /**
     * @brief 启动模块业务。
     *
     * @return 成功返回 `Ok`；失败时返回启动错误。
     */
    virtual foundation::base::Result<void> Start() = 0;
    /**
     * @brief 停止模块业务。
     *
     * @return 成功返回 `Ok`；失败时返回停止错误。
     */
    virtual foundation::base::Result<void> Stop() = 0;
    /**
     * @brief 释放模块资源。
     *
     * @return 成功返回 `Ok`；失败时返回反初始化错误。
     */
    virtual foundation::base::Result<void> Fini() = 0;

    /**
     * @brief 查询模块当前生命周期状态。
     *
     * @return 当前模块状态。
     */
    virtual ModuleState State() const = 0;
};

}  // namespace framework
}  // namespace module_context
