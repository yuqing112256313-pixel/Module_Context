#pragma once

#include "module_context/framework/Export.h"
#include "module_context/framework/IModule.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/base/Result.h"
#include "foundation/config/ConfigValue.h"

#include <string>

namespace module_context {
namespace framework {

class IContext;

/**
 * @brief 模块装载与模块实例查询接口。
 *
 * 模块管理器负责把配置中的模块声明转化为运行时模块实例，并统一驱动这些实例的
 * 生命周期。它关注“模块对象本身”的管理；面向业务能力的发现统一通过
 * `IContext::GetService()` 完成。
 *
 * 当前实现按装载顺序执行 `Init()` / `Start()`，按逆序执行 `Stop()` / `Fini()`，
 * 用这种栈式语义表达模块间依赖：先加载的基础模块可被后加载模块依赖。
 */
class MC_FRAMEWORK_API IModuleManager {
public:
    /**
     * @brief 析构模块管理器接口。
     */
    virtual ~IModuleManager() {}

    /**
     * @brief 根据配置文件批量加载模块。
     *
     * 配置文件使用当前框架支持的 schema，至少应包含 `schema_version` 和 `modules`。
     * `modules[*].name` 是实例名，`modules[*].type` 是插件声明的模块类型名，
     * `modules[*].library_path` 是插件动态库路径。
     *
     * @param config_file_path 模块配置文件路径。
     * @return 成功返回 `Ok`；失败时返回配置解析或插件加载错误。
     */
    virtual foundation::base::Result<void> LoadModules(
        const std::string& config_file_path) = 0;
    /**
     * @brief 直接加载单个模块实例。
     *
     * 该接口适合测试、工具程序或由调用方自行管理配置的场景。通过该接口加载的模块
     * 会得到一个空对象配置。
     *
     * @param name 模块实例名。
     * @param library_path 模块动态库路径。
     * @return 成功返回 `Ok`；失败时返回装载错误。
     */
    virtual foundation::base::Result<void> LoadModule(
        const std::string& name,
        const std::string& library_path) = 0;

    /**
     * @brief 按加载顺序初始化已装载模块。
     *
     * @param ctx 所属上下文。
     * @return 成功返回 `Ok`；失败时返回第一个初始化错误。
     */
    virtual foundation::base::Result<void> Init(IContext& ctx) = 0;
    /**
     * @brief 按加载顺序启动已装载模块。
     *
     * @return 成功返回 `Ok`；失败时返回第一个启动错误。
     */
    virtual foundation::base::Result<void> Start() = 0;
    /**
     * @brief 按逆序停止已启动模块。
     *
     * 未启动或启动失败的模块不会执行 `Stop()`，其初始化资源由后续 `Fini()` 释放。
     *
     * @return 成功返回 `Ok`；失败时返回首个停止错误。
     */
    virtual foundation::base::Result<void> Stop() = 0;
    /**
     * @brief 按逆序释放已装载模块。
     *
     * @return 成功返回 `Ok`；失败时返回首个反初始化错误。
     */
    virtual foundation::base::Result<void> Fini() = 0;

    /**
     * @brief 读取指定模块实例的配置对象。
     *
     * 返回的是配置文件中 `modules[*].config` 对象的副本。若模块通过 `LoadModule()`
     * 直接加载，则返回空对象。
     *
     * @param name 模块实例名。
     * @return 成功时返回配置对象；未找到时返回 `kNotFound`。
     */
    virtual foundation::base::Result<foundation::config::ConfigValue> ModuleConfig(
        const std::string& name) = 0;

    /**
     * @brief 按模块实例名查询并转换为指定类型。
     *
     * @tparam T 期望的模块接口类型。
     * @param name 模块实例名。
     * @return 成功时返回类型转换后的模块指针；未找到或类型不匹配时返回错误。
     *
     * @note 该接口用于管理模块实例本身。若调用方只需要某种业务能力，优先使用
     *       `IContext::GetService<T>()`，以保持模块实现与能力接口解耦。
     */
    template <typename T>
    foundation::base::Result<T*> Module(const std::string& name) {
        foundation::base::Result<IModule*> module = LookupModuleRaw(name);
        if (!module.IsOk()) {
            return foundation::base::Result<T*>(
                module.GetError(),
                module.GetMessage());
        }

        T* typed_module = dynamic_cast<T*>(module.Value());
        if (typed_module == NULL) {
            return foundation::base::Result<T*>(
                foundation::base::ErrorCode::kInvalidState,
                "Module type cast failed");
        }

        return foundation::base::Result<T*>(typed_module);
    }

private:
    /**
     * @brief 按实例名查询原始模块指针。
     *
     * @param name 模块实例名。
     * @return 成功时返回模块接口指针；失败时返回错误。
     */
    virtual foundation::base::Result<IModule*> LookupModuleRaw(
        const std::string& name) = 0;
};

}  // namespace framework
}  // namespace module_context
