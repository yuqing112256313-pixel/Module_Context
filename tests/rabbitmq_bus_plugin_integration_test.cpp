#include "module_context/framework/IContext.h"
#include "module_context/framework/IModuleManager.h"
#include "module_context/messaging/IMessageBusService.h"
#include "framework/Context.h"

#include "foundation/base/ErrorCode.h"

#include <iostream>
#include <string>

namespace {

bool Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

bool RunSingleServiceCase() {
    module_context::framework::Context context;
    module_context::framework::IModuleManager* manager = context.ModuleManager();
    if (!Expect(manager != NULL, "Context should expose a module manager")) {
        return false;
    }

    const std::string instance_name = "rabbitmq_bus_instance";
    foundation::base::Result<void> load_result =
        manager->LoadModule(instance_name, MC_TEST_RABBITMQ_BUS_PLUGIN_PATH);
    if (!Expect(load_result.IsOk(), "LoadModule should load rabbitmq_bus plugin")) {
        return false;
    }

    foundation::base::Result<module_context::framework::IModule*> module =
        manager->Module<module_context::framework::IModule>(instance_name);
    if (!Expect(module.IsOk(), "Module should be queryable by injected instance name")) {
        return false;
    }

    if (!Expect(module.Value()->ModuleName() == instance_name,
                "ModuleName should return injected instance name")) {
        return false;
    }

    if (!Expect(module.Value()->ModuleType() == "rabbitmq_bus",
                "ModuleType should return the plugin type name")) {
        return false;
    }

    foundation::base::Result<module_context::framework::IContext*> wrong_type =
        manager->Module<module_context::framework::IContext>(instance_name);
    if (!Expect(
            !wrong_type.IsOk() &&
                wrong_type.GetError() == foundation::base::ErrorCode::kInvalidState,
            "Module<IContext> should fail with kInvalidState")) {
        return false;
    }

    foundation::base::Result<module_context::messaging::IMessageBusService*> named_bus =
        context.GetService<module_context::messaging::IMessageBusService>(instance_name);
    if (!Expect(named_bus.IsOk(), "GetService<IMessageBusService>(name) should succeed")) {
        return false;
    }

    foundation::base::Result<module_context::messaging::IMessageBusService*> unique_bus =
        context.GetService<module_context::messaging::IMessageBusService>();
    if (!Expect(unique_bus.IsOk(), "GetService<IMessageBusService>() should succeed")) {
        return false;
    }

    if (!Expect(named_bus.Value() == unique_bus.Value(),
                "Named and unique service lookup should return the same instance")) {
        return false;
    }

    if (!Expect(
            named_bus.Value()->GetConnectionState() ==
                module_context::messaging::ConnectionState::Created,
            "Freshly loaded rabbitmq_bus service should be in Created state")) {
        return false;
    }

    foundation::base::Result<void> fini_result = context.Fini();
    if (!Expect(fini_result.IsOk(), "Fini should unload rabbitmq_bus plugin")) {
        return false;
    }

    foundation::base::Result<module_context::messaging::IMessageBusService*> after_fini =
        context.GetService<module_context::messaging::IMessageBusService>(instance_name);
    if (!Expect(
            !after_fini.IsOk() &&
                after_fini.GetError() == foundation::base::ErrorCode::kNotFound,
            "GetService(name) should return kNotFound after Fini")) {
        return false;
    }

    return true;
}

bool RunMultipleServiceCase() {
    module_context::framework::Context context;
    module_context::framework::IModuleManager* manager = context.ModuleManager();
    if (!Expect(manager != NULL, "Context should expose a module manager")) {
        return false;
    }

    foundation::base::Result<void> first_load =
        manager->LoadModule("bus_primary", MC_TEST_RABBITMQ_BUS_PLUGIN_PATH);
    if (!Expect(first_load.IsOk(), "Primary rabbitmq_bus plugin should load")) {
        return false;
    }

    foundation::base::Result<void> second_load =
        manager->LoadModule("bus_backup", MC_TEST_RABBITMQ_BUS_PLUGIN_PATH);
    if (!Expect(second_load.IsOk(), "Backup rabbitmq_bus plugin should load")) {
        return false;
    }

    foundation::base::Result<module_context::messaging::IMessageBusService*> primary_bus =
        context.GetService<module_context::messaging::IMessageBusService>("bus_primary");
    if (!Expect(primary_bus.IsOk(), "Primary service lookup should succeed")) {
        return false;
    }

    foundation::base::Result<module_context::messaging::IMessageBusService*> backup_bus =
        context.GetService<module_context::messaging::IMessageBusService>("bus_backup");
    if (!Expect(backup_bus.IsOk(), "Backup service lookup should succeed")) {
        return false;
    }

    if (!Expect(primary_bus.Value() != backup_bus.Value(),
                "Named service lookup should distinguish separate instances")) {
        return false;
    }

    foundation::base::Result<module_context::messaging::IMessageBusService*> unique_bus =
        context.GetService<module_context::messaging::IMessageBusService>();
    if (!Expect(
            !unique_bus.IsOk() &&
                unique_bus.GetError() == foundation::base::ErrorCode::kInvalidState,
            "GetService() should fail with kInvalidState when multiple services exist")) {
        return false;
    }

    foundation::base::Result<void> fini_result = context.Fini();
    if (!Expect(fini_result.IsOk(), "Fini should unload all rabbitmq_bus plugins")) {
        return false;
    }

    return true;
}

}  // namespace

int main() {
    if (!RunSingleServiceCase()) {
        return 1;
    }
    if (!RunMultipleServiceCase()) {
        return 1;
    }

    std::cout << "[PASSED] rabbitmq_bus_plugin_integration_test" << std::endl;
    return 0;
}
