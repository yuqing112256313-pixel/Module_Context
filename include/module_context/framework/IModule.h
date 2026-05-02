#pragma once

#include "module_context/framework/Export.h"
#include "module_context/framework/ModuleState.h"

#include "foundation/base/Result.h"

#include <string>
#include <vector>

namespace module_context {
namespace framework {

class IContext;

/**
 * @brief 模块运行时对象的公开抽象接口。
 *
 * 模块是框架调度的最小运行单元，由插件工厂创建、由模块管理器保存并驱动生命周期。
 * 一个模块可以只承担内部职责，也可以通过继承某个服务接口向上下文暴露能力。
 *
 * 外部代码通常不直接依赖模块实现类：需要管理模块实例时通过
 * `IModuleManager::Module<T>()` 查询，需要使用业务能力时通过 `IContext::GetService<T>()`
 * 查询。
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
     * 实例名来自配置中的 `name` 字段，用于区分同一模块类型的多个实例。
     *
     * @return 配置中声明的模块实例名；若尚未注入实例名，可退化为模块类型名。
     */
    virtual std::string ModuleName() const = 0;
    /**
     * @brief 返回模块类型名。
     *
     * 类型名是插件实现声明的稳定逻辑名称，用于和配置中的 `type` 字段对齐。
     *
     * @return 模块类型名。
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
     * @note 该方法由框架在进入生命周期前调用。模块进入 `Init()` 后实例名应保持稳定。
     */
    virtual foundation::base::Result<void> SetModuleName(
        const std::string& name) = 0;

    /**
     * @brief 初始化模块资源。
     *
     * @param ctx 所属上下文。
     * @return 成功返回 `Ok`；失败时返回初始化错误。
     *
     * @note `Init()` 应只完成资源准备和依赖绑定，不应启动长时间运行的业务循环。
     */
    virtual foundation::base::Result<void> Init(IContext& ctx) = 0;
    /**
     * @brief 启动模块业务。
     *
     * @return 成功返回 `Ok`；失败时返回启动错误。
     *
     * @note `Start()` 应在 `Init()` 成功后调用，用于启动线程、连接、订阅等运行态行为。
     */
    virtual foundation::base::Result<void> Start() = 0;
    /**
     * @brief 停止模块业务。
     *
     * @return 成功返回 `Ok`；失败时返回停止错误。
     *
     * @note `Stop()` 用于清理运行态资源，不释放 `Init()` 阶段建立的基础资源。
     */
    virtual foundation::base::Result<void> Stop() = 0;
    /**
     * @brief 释放模块资源。
     *
     * @return 成功返回 `Ok`；失败时返回反初始化错误。
     *
     * @note `Fini()` 是模块生命周期的终点。调用后模块实例不应继续对外提供服务。
     */
    virtual foundation::base::Result<void> Fini() = 0;

    /**
     * @brief 查询模块当前生命周期状态。
     *
     * @return 当前模块状态。
     */
    virtual ModuleState State() const = 0;
};

/**
 * @brief 模块类型元信息的可选扩展接口。
 *
 * 框架加载模块时，`IModule::ModuleType()` 是插件的标准类型名。模块若经历过
 * 类型名迁移，可以额外实现该接口返回兼容别名，让旧配置继续通过类型校验。
 *
 * 该接口独立于 `IModule`，避免给所有既有插件增加新的纯虚函数；模块管理器会在
 * 校验阶段通过 `dynamic_cast` 探测是否支持。
 */
class MC_FRAMEWORK_API IModuleTypeMetadata {
public:
    /**
     * @brief 析构元信息接口。
     */
    virtual ~IModuleTypeMetadata() {}

    /**
     * @brief 返回该模块类型接受的兼容别名。
     *
     * @return 兼容类型名列表，不应包含 `IModule::ModuleType()` 返回的标准类型名。
     */
    virtual std::vector<std::string> ModuleTypeAliases() const = 0;
};

}  // namespace framework
}  // namespace module_context
