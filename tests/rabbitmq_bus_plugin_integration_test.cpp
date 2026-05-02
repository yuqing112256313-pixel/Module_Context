#include "module_context/framework/IContext.h"
#include "module_context/framework/IModuleManager.h"
#include "module_context/messaging/IMessageBusService.h"
#include "framework/Context.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/filesystem/FileUtils.h"
#include "foundation/filesystem/PathUtils.h"

#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>

namespace {

bool Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

std::string JsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string ConfigPath(const std::string& file_name) {
    return foundation::filesystem::Join(MC_TEST_BINARY_DIR, file_name);
}

bool WriteConfigFile(const std::string& file_name, const std::string& content) {
    foundation::base::Result<void> write_result =
        foundation::filesystem::WriteAllText(ConfigPath(file_name), content);
    return Expect(
        write_result.IsOk(),
        "Failed to write config file '" + file_name + "'");
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

    if (!Expect(module.Value()->ModuleType() == "amqp_bus",
                "ModuleType should return the platform plugin type name")) {
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
            "Freshly loaded AMQP bus service should be in Created state")) {
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

bool RunConfigTypeAliasCase() {
    std::ostringstream config;
    config
        << "{\n"
        << "  \"schema_version\": 2,\n"
        << "  \"modules\": [\n"
        << "    {\n"
        << "      \"name\": \"amqp_bus_instance\",\n"
        << "      \"type\": \"amqp_bus\",\n"
        << "      \"library_path\": \""
        << JsonEscape(MC_TEST_RABBITMQ_BUS_PLUGIN_PATH) << "\",\n"
        << "      \"config\": {}\n"
        << "    },\n"
        << "    {\n"
        << "      \"name\": \"legacy_rabbitmq_bus_instance\",\n"
        << "      \"type\": \"rabbitmq_bus\",\n"
        << "      \"library_path\": \""
        << JsonEscape(MC_TEST_RABBITMQ_BUS_PLUGIN_PATH) << "\",\n"
        << "      \"config\": {}\n"
        << "    }\n"
        << "  ]\n"
        << "}\n";

    if (!WriteConfigFile("rabbitmq_bus_alias_config.json", config.str())) {
        return false;
    }

    module_context::framework::Context context;
    module_context::framework::IModuleManager* manager = context.ModuleManager();
    if (!Expect(manager != NULL, "Context should expose a module manager")) {
        return false;
    }

    foundation::base::Result<void> load_result =
        manager->LoadModules(ConfigPath("rabbitmq_bus_alias_config.json"));
    if (!Expect(
            load_result.IsOk(),
            "LoadModules should accept amqp_bus and legacy rabbitmq_bus types")) {
        return false;
    }

    foundation::base::Result<module_context::framework::IModule*> platform_module =
        manager->Module<module_context::framework::IModule>("amqp_bus_instance");
    foundation::base::Result<module_context::framework::IModule*> legacy_module =
        manager->Module<module_context::framework::IModule>(
            "legacy_rabbitmq_bus_instance");
    if (!Expect(
            platform_module.IsOk() && legacy_module.IsOk(),
            "Both platform and legacy type entries should load")) {
        return false;
    }
    if (!Expect(
            platform_module.Value()->ModuleType() == "amqp_bus" &&
                legacy_module.Value()->ModuleType() == "amqp_bus",
            "Both entries should expose amqp_bus as the standard runtime type")) {
        return false;
    }

    foundation::base::Result<void> fini_result = context.Fini();
    if (!Expect(fini_result.IsOk(), "Fini should unload alias config modules")) {
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
    if (!RunConfigTypeAliasCase()) {
        return 1;
    }
    if (!RunMultipleServiceCase()) {
        return 1;
    }

    std::cout << "[PASSED] rabbitmq_bus_plugin_integration_test" << std::endl;
    return 0;
}
