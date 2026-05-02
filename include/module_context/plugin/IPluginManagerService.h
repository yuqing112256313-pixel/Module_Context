#pragma once

#include "module_context/framework/Export.h"
#include "module_context/plugin/Types.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/base/Result.h"

#include "iplugin.h"

#include <string>

namespace module_context {
namespace plugin {

/**
 * @brief 工序插件管理服务公开接口。
 *
 * 外部使用者通过 `IContext::GetService<IPluginManagerService>()` 获取该接口，
 * 再通过 `GetPlugin<T>(name)` 按工序类型获取底层插件实例指针。
 *
 * 该接口描述的是业务/算法插件管理能力，不是模块框架自身的动态库 ABI。
 * 在当前系统中，`semiplugin_manager` 本身是一个框架模块；它再向下管理
 * SEMI 工序插件。业务层仅依赖本接口，不感知底层 DLL 加载细节。
 */
class MC_FRAMEWORK_API IPluginManagerService {
public:
    /**
     * @brief 析构服务接口。
     */
    virtual ~IPluginManagerService() {}

    /**
     * @brief 查询指定插件的运行时状态。
     *
     * @param name 插件实例名，与配置中的 `name` 字段对应。
     * @return 插件当前的生命周期状态。
     */
    virtual PluginState GetPluginState(const std::string& name) const = 0;

    /**
     * @brief 按类型和实例名获取底层工序插件接口指针。
     *
     * @tparam T 工序插件接口类型，例如 `Hh::Api::Plugin::IPluginTGVEtching`。
     * @param name 插件实例名，与配置中的 `name` 字段对应。
     * @return 成功时返回插件接口指针；未找到或类型不匹配时返回错误。
     *
     * @note 返回的指针生命周期由插件管理模块负责，
     *       调用方不得持久持有或在模块 Stop 后继续使用。
     */
    template <typename T>
    foundation::base::Result<T*> GetPlugin(const std::string& name) {
        foundation::base::Result<Hh::Api::Plugin::IPlugin*> raw =
            LookupPluginRaw(name);
        if (!raw.IsOk()) {
            return foundation::base::Result<T*>(
                raw.GetError(),
                raw.GetMessage());
        }

        T* typed = dynamic_cast<T*>(raw.Value());
        if (typed == NULL) {
            return foundation::base::Result<T*>(
                foundation::base::ErrorCode::kInvalidState,
                "Plugin type cast failed for '" + name + "'");
        }

        return foundation::base::Result<T*>(typed);
    }

private:
    /**
     * @brief 供模板方法调用的原始插件查找钩子。
     *
     * @param name 插件实例名。
     * @return 成功时返回 IPlugin 基类指针；未找到时返回 kNotFound 错误。
     */
    virtual foundation::base::Result<Hh::Api::Plugin::IPlugin*> LookupPluginRaw(
        const std::string& name) = 0;
};

}  // namespace plugin
}  // namespace module_context

namespace module_context {
namespace framework {

template <typename T>
struct ServiceTypeTraits;

/**
 * @brief `IPluginManagerService` 的服务键映射。
 */
template <>
struct ServiceTypeTraits<module_context::plugin::IPluginManagerService> {
    static const char* Key() {
        return "module_context.plugin.IPluginManagerService";
    }
};

}  // namespace framework
}  // namespace module_context
