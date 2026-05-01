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

    foundation::base::Result<ModuleHandle> CreateModuleHandle(
        const std::string& normalized_library_path);
    foundation::base::Result<IModule*> LookupModuleRaw(
        const std::string& name) override;
    void RegisterKnownServices(
        const std::string& name,
        IModule* module);
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
