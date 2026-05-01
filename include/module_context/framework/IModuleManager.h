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
 * 该接口负责模块配置加载、插件实例创建、生命周期驱动以及模块实例级查询。
 * 服务接口发现统一通过 `IContext::GetService()` 完成，不在此接口暴露。
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
     * @param config_file_path 模块配置文件路径。
     * @return 成功返回 `Ok`；失败时返回配置解析或插件加载错误。
     */
    virtual foundation::base::Result<void> LoadModules(
        const std::string& config_file_path) = 0;
    /**
     * @brief 直接加载单个模块实例。
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
     * @brief 按逆序停止已装载模块。
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
