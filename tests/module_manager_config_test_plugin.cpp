#include "module_context/framework/IModuleManager.h"
#include "plugin/ModuleFactory.h"
#include "framework/ModuleBase.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/base/Result.h"
#include "foundation/config/ConfigValue.h"

#include <string>

namespace {

class ConfigTestModule : public module_context::framework::ModuleBase {
public:
    std::string ModuleType() const override {
        return "config_test_module";
    }

    std::string ModuleVersion() const override {
        return "1.0.0";
    }

protected:
    foundation::base::Result<void> OnInit() override {
        module_context::framework::IModuleManager* manager = Context().ModuleManager();
        if (manager == NULL) {
            return foundation::base::Result<void>(
                foundation::base::ErrorCode::kInvalidState,
                "ConfigTestModule requires a module manager");
        }

        foundation::base::Result<foundation::config::ConfigValue> config =
            manager->ModuleConfig(ModuleName());
        if (!config.IsOk()) {
            return foundation::base::Result<void>(
                config.GetError(),
                config.GetMessage());
        }

        foundation::base::Result<foundation::config::ConfigValue> role =
            config.Value().ObjectGet("role");
        if (!role.IsOk() || !role.Value().AsString().IsOk()) {
            return foundation::base::Result<void>(
                foundation::base::ErrorCode::kParseError,
                "ConfigTestModule requires string field 'role'");
        }

        return foundation::base::MakeSuccess();
    }
};

}  // namespace

MC_DECLARE_MODULE_FACTORY(ConfigTestModule)
