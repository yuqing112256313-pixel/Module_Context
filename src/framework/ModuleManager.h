#pragma once

#include "module_context/framework/IModule.h"
#include "module_context/framework/IModuleManager.h"

#include "framework/ServiceRegistry.h"

#include "foundation/base/NonCopyable.h"
#include "foundation/base/Result.h"
#include "foundation/config/ConfigValue.h"
#include "foundation/plugin/PluginLoader.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace module_context {
namespace framework {

/**
 * @brief 模块管理器默认实现。
 *
 * 该类把配置解析、插件装载、模块实例保存、生命周期驱动和服务注册放在同一个边界内。
 * 对外仍只暴露 `IModuleManager` 的模块级能力；服务发现由 `Context` 转发到内部
 * `ServiceRegistry`，避免调用方把“模块类型”和“业务能力”混为一谈。
 */
class MC_FRAMEWORK_API ModuleManager final
    : public IModuleManager,
      private foundation::base::NonCopyable {
public:
    ModuleManager();
    ~ModuleManager() override;

    foundation::base::Result<void> LoadModules(
        const std::string& config_file_path) override;
    foundation::base::Result<void> LoadModule(
        const std::string& name,
        const std::string& library_path) override;

    foundation::base::Result<void> Init(IContext& ctx) override;
    foundation::base::Result<void> Start() override;
    foundation::base::Result<void> Stop() override;
    foundation::base::Result<void> Fini() override;

    foundation::base::Result<foundation::config::ConfigValue> ModuleConfig(
        const std::string& name) override;
    foundation::base::Result<IModule*> LookupNamedService(
        const std::string& service_key,
        const std::string& name);
    foundation::base::Result<IModule*> LookupUniqueService(
        const std::string& service_key);

private:
    typedef foundation::plugin::PluginLoader<IModule> ModuleLoader;
    typedef ModuleLoader::PluginHandle ModuleHandle;

    // 打开插件库并创建模块实例。返回的句柄同时负责实例与动态库生命周期。
    foundation::base::Result<ModuleHandle> CreateModuleHandle(
        const std::string& normalized_library_path);
    // IModuleManager::Module<T>() 的非模板查找入口。
    foundation::base::Result<IModule*> LookupModuleRaw(
        const std::string& name) override;
    // 根据模块实现继承的已知服务接口，把实例登记为对应能力的提供者。
    void RegisterKnownServices(
        const std::string& name,
        IModule* module);
    // 成功装载后的唯一提交点，保证模块表、顺序表和服务注册同步更新。
    void StoreLoadedModule(
        const std::string& name,
        ModuleHandle module);

private:
    typedef std::unordered_map<std::string, ModuleHandle> ModuleMap;
    typedef std::unordered_map<std::string, foundation::config::ConfigValue> ModuleConfigMap;
    typedef std::vector<std::string> ModuleOrder;

    ModuleMap modules_by_name_;
    ModuleConfigMap configs_by_name_;
    ModuleOrder module_order_;
    ServiceRegistry service_registry_;
};

}  // namespace framework
}  // namespace module_context
