#pragma once

#include "module_context/plugin/IPluginManagerService.h"
#include "framework/ModuleBase.h"

#include "foundation/base/Result.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace module_context {
namespace plugin {

struct LoadedPlugin;
struct PluginEntryConfig;
class PluginLoader;
class PluginRegistry;

/**
 * @brief 工序插件管理模块。
 *
 * 继承 `ModuleBase`（生命周期驱动）和 `IPluginManagerService`（对外服务接口），
 *   - `OnInit`  读取配置 → 批量加载 DLL → 调用各插件 Init()
 *   - `OnStart` 按加载顺序调用各插件 Start()
 *   - `OnStop`  逆序调用各插件 Stop()
 *   - `OnFini`  逆序调用各插件 Fini() → 卸载 DLL
 *
 * 业务层通过 `IContext::GetService<IPluginManagerService>()` 获取本模块引用，
 * 再调用 `GetPlugin<T>(name)` 按工序类型获取底层插件指针。
 */
class SemipluginManagerModule final
    : public module_context::framework::ModuleBase,
      public IPluginManagerService {
public:
    SemipluginManagerModule();
    ~SemipluginManagerModule() override;

    std::string ModuleType() const override;
    std::string ModuleVersion() const override;

    PluginState GetPluginState(const std::string& name) const override;

protected:
    foundation::base::Result<void> OnInit() override;
    foundation::base::Result<void> OnStart() override;
    foundation::base::Result<void> OnStop() override;
    foundation::base::Result<void> OnFini() override;

private:
    foundation::base::Result<Hh::Api::Plugin::IPlugin*> LookupPluginRaw(
        const std::string& name) override;

    foundation::base::Result<void> LoadAndInitPlugin(
        const PluginEntryConfig& entry_config);
    foundation::base::Result<void> StopPluginsInReverse(
        const std::vector<std::string>& plugin_names);

private:
    std::unique_ptr<PluginLoader> loader_;
    std::unique_ptr<PluginRegistry> registry_;

    std::vector<std::string> plugin_load_order_;
    std::map<std::string, LoadedPlugin> loaded_plugins_;
    std::map<std::string, PluginState> plugin_states_;
};

}  // namespace plugin
}  // namespace module_context
