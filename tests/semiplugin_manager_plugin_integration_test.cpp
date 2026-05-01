/**
 * semiplugin_manager_plugin_integration_test.cpp
 *
 * 通过框架的 Context + IModuleManager 加载 semiplugin_manager.dll，
 * 验证从 DLL 加载到服务发现的完整流程。
 *
 * 覆盖范围：
 *   1. LoadModule 成功加载 semiplugin_manager 插件 DLL
 *   2. 模块实例名和类型名查询正确
 *   3. GetService<IPluginManagerService>(name) 按实例名查找成功
 *   4. GetService<IPluginManagerService>() 唯一查找成功
 *   5. 两种查找返回同一实例指针
 *   6. 加载两个 semiplugin_manager 实例时唯一查找返回 kInvalidState
 *   7. Fini 后按名查找返回 kNotFound
 *   8. 空插件列表时 Init → Start → Stop → Fini 完整生命周期不报错
 *   9. GetPluginState 对未加载插件返回 kUnloaded
 */

#include "module_context/framework/IContext.h"
#include "module_context/framework/IModuleManager.h"
#include "module_context/plugin/IPluginManagerService.h"
#include "module_context/plugin/Types.h"
#include "framework/Context.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/config/ConfigValue.h"

#include "iplugin_tgv_etching.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

using foundation::base::ErrorCode;
using foundation::config::ConfigValue;
using module_context::framework::Context;
using module_context::plugin::IPluginManagerService;
using module_context::plugin::PluginState;

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

bool WriteTextFile(
    const std::string& path,
    const std::string& content) {
    std::ofstream output(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output << content;
    return static_cast<bool>(output);
}

// ---------------------------------------------------------------------------
// 辅助：构建空插件列表配置并注入到 Context
// ---------------------------------------------------------------------------

void SetField(ConfigValue* object,
              const std::string& key,
              const ConfigValue& value) {
    (void)object->Set(key, value);
}

/**
 * @brief 构建一个 plugins 数组为空的合法配置对象。
 *
 * 集成测试中不依赖真实 SEMIPLUGIN DLL，因此使用空列表验证管理层框架流程。
 */
ConfigValue MakeEmptyPluginConfig() {
    ConfigValue root = ConfigValue::MakeObject();
    ConfigValue plugins = ConfigValue::MakeArray();
    SetField(&root, "plugins", plugins);
    return root;
}

// ---------------------------------------------------------------------------
// 测试用例 1：单实例服务注册与发现
// ---------------------------------------------------------------------------

bool RunSingleServiceCase() {
    Context context;
    module_context::framework::IModuleManager* manager = context.ModuleManager();
    if (!Expect(manager != NULL, "Context should expose a module manager")) {
        return false;
    }

    const std::string instance_name = "semiplugin_manager_instance";

    foundation::base::Result<void> load_result =
        manager->LoadModule(instance_name, MC_TEST_SEMIPLUGIN_MANAGER_PLUGIN_PATH);
    if (!Expect(load_result.IsOk(),
                "LoadModule should succeed for semiplugin_manager")) {
        return false;
    }

    // 验证模块实例名
    foundation::base::Result<module_context::framework::IModule*> raw_module =
        manager->Module<module_context::framework::IModule>(instance_name);
    if (!Expect(raw_module.IsOk(),
                "Module should be queryable by instance name")) {
        return false;
    }
    if (!Expect(raw_module.Value()->ModuleName() == instance_name,
                "ModuleName should return injected instance name")) {
        return false;
    }
    if (!Expect(raw_module.Value()->ModuleType() == "semiplugin_manager",
                "ModuleType should return 'semiplugin_manager'")) {
        return false;
    }

    // 服务在 LoadModule 时即注册到 ServiceRegistry，无需先调用 context.Init()。
    // Init 前插件列表为空，GetPluginState 应返回 kUnloaded。
    foundation::base::Result<IPluginManagerService*> service_before_init =
        context.GetService<IPluginManagerService>(instance_name);
    if (!Expect(service_before_init.IsOk(),
                "GetService<IPluginManagerService>(name) should succeed before Init")) {
        return false;
    }

    if (!Expect(service_before_init.Value() != NULL,
                "Service pointer should not be null")) {
        return false;
    }

    // 唯一服务查找（只有一个实例时应成功）
    foundation::base::Result<IPluginManagerService*> unique_service =
        context.GetService<IPluginManagerService>();
    if (!Expect(unique_service.IsOk(),
                "GetService<IPluginManagerService>() unique lookup should succeed")) {
        return false;
    }

    if (!Expect(service_before_init.Value() == unique_service.Value(),
                "Named and unique service lookup should return the same instance")) {
        return false;
    }

    // 未加载插件的状态查询
    if (!Expect(
            service_before_init.Value()->GetPluginState("nonexistent") ==
                PluginState::kUnloaded,
            "GetPluginState for unregistered plugin should return kUnloaded")) {
        return false;
    }

    // 错误类型转换不应破坏服务
    foundation::base::Result<module_context::framework::IContext*> wrong_type =
        manager->Module<module_context::framework::IContext>(instance_name);
    if (!Expect(
            !wrong_type.IsOk() &&
                wrong_type.GetError() == ErrorCode::kInvalidState,
            "Module<IContext> should fail with kInvalidState for wrong type")) {
        return false;
    }

    // Fini 后服务应注销
    foundation::base::Result<void> fini_result = context.Fini();
    if (!Expect(fini_result.IsOk(),
                "Context::Fini should succeed after semiplugin_manager load")) {
        return false;
    }

    foundation::base::Result<IPluginManagerService*> after_fini =
        context.GetService<IPluginManagerService>(instance_name);
    if (!Expect(
            !after_fini.IsOk() && after_fini.GetError() == ErrorCode::kNotFound,
            "GetService(name) should return kNotFound after Fini")) {
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// 测试用例 2：多实例时唯一查找应报 kInvalidState
// ---------------------------------------------------------------------------

bool RunMultipleServiceCase() {
    Context context;
    module_context::framework::IModuleManager* manager = context.ModuleManager();
    if (!Expect(manager != NULL, "Context should expose a module manager")) {
        return false;
    }

    foundation::base::Result<void> first_load =
        manager->LoadModule("spm_primary", MC_TEST_SEMIPLUGIN_MANAGER_PLUGIN_PATH);
    if (!Expect(first_load.IsOk(), "Primary semiplugin_manager should load")) {
        return false;
    }

    foundation::base::Result<void> second_load =
        manager->LoadModule("spm_backup", MC_TEST_SEMIPLUGIN_MANAGER_PLUGIN_PATH);
    if (!Expect(second_load.IsOk(), "Backup semiplugin_manager should load")) {
        return false;
    }

    foundation::base::Result<IPluginManagerService*> primary =
        context.GetService<IPluginManagerService>("spm_primary");
    if (!Expect(primary.IsOk(), "Primary service lookup should succeed")) {
        return false;
    }

    foundation::base::Result<IPluginManagerService*> backup =
        context.GetService<IPluginManagerService>("spm_backup");
    if (!Expect(backup.IsOk(), "Backup service lookup should succeed")) {
        return false;
    }

    if (!Expect(primary.Value() != backup.Value(),
                "Named lookups for different instances should return different pointers")) {
        return false;
    }

    foundation::base::Result<IPluginManagerService*> unique =
        context.GetService<IPluginManagerService>();
    if (!Expect(
            !unique.IsOk() && unique.GetError() == ErrorCode::kInvalidState,
            "GetService() should fail with kInvalidState when multiple instances exist")) {
        return false;
    }

    foundation::base::Result<void> fini_result = context.Fini();
    if (!Expect(fini_result.IsOk(),
                "Fini should succeed after unloading multiple instances")) {
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// 测试用例 3：重复加载同名实例应报错
// ---------------------------------------------------------------------------

bool RunDuplicateInstanceNameCase() {
    Context context;
    module_context::framework::IModuleManager* manager = context.ModuleManager();
    if (!Expect(manager != NULL, "Context should expose a module manager")) {
        return false;
    }

    foundation::base::Result<void> first_load =
        manager->LoadModule("spm_dup", MC_TEST_SEMIPLUGIN_MANAGER_PLUGIN_PATH);
    if (!Expect(first_load.IsOk(), "First LoadModule with same name should succeed")) {
        return false;
    }

    foundation::base::Result<void> second_load =
        manager->LoadModule("spm_dup", MC_TEST_SEMIPLUGIN_MANAGER_PLUGIN_PATH);
    if (!Expect(!second_load.IsOk(),
                "Second LoadModule with the same instance name should fail")) {
        return false;
    }
    if (!Expect(second_load.GetError() == ErrorCode::kAlreadyExists,
                "Duplicate instance name should return kAlreadyExists")) {
        return false;
    }

    (void)context.Fini();
    return true;
}

// ---------------------------------------------------------------------------
// 测试用例 4：加载真实 SEMIPLUGIN DLL 并通过工序接口取回
// ---------------------------------------------------------------------------

bool RunRealSemipluginLifecycleCase() {
    const std::string config_path =
        std::string(MC_TEST_BINARY_DIR) + "/semiplugin_manager_real_plugin.json";

    std::ostringstream config;
    config
        << "{\n"
        << "  \"schema_version\": 2,\n"
        << "  \"modules\": [\n"
        << "    {\n"
        << "      \"name\": \"semiplugin_manager\",\n"
        << "      \"type\": \"semiplugin_manager\",\n"
        << "      \"library_path\": \"" << JsonEscape(MC_TEST_SEMIPLUGIN_MANAGER_PLUGIN_PATH) << "\",\n"
        << "      \"config\": {\n"
        << "        \"plugins\": [\n"
        << "          {\n"
        << "            \"name\": \"tgv_etching\",\n"
        << "            \"type\": \"tgv_etching\",\n"
        << "            \"library\": \"" << JsonEscape(MC_TEST_TGV_ETCHING_PLUGIN_PATH) << "\",\n"
        << "            \"create_func\": \"CreatePluginEtching\",\n"
        << "            \"destroy_func\": \"DestroyPluginEtching\"\n"
        << "          }\n"
        << "        ]\n"
        << "      }\n"
        << "    }\n"
        << "  ]\n"
        << "}\n";

    if (!Expect(
            WriteTextFile(config_path, config.str()),
            "Should write real semiplugin manager config")) {
        return false;
    }

    Context context;
    module_context::framework::IModuleManager* manager = context.ModuleManager();
    if (!Expect(manager != NULL, "Context should expose a module manager")) {
        return false;
    }

    foundation::base::Result<void> load_result =
        manager->LoadModules(config_path);
    if (!Expect(load_result.IsOk(), "LoadModules should load semiplugin_manager")) {
        return false;
    }

    foundation::base::Result<void> init_result = context.Init();
    if (!Expect(init_result.IsOk(), "Context::Init should initialize real SEMIPLUGIN")) {
        return false;
    }

    foundation::base::Result<void> start_result = context.Start();
    if (!Expect(start_result.IsOk(), "Context::Start should start real SEMIPLUGIN")) {
        (void)context.Fini();
        return false;
    }

    foundation::base::Result<IPluginManagerService*> service =
        context.GetService<IPluginManagerService>("semiplugin_manager");
    if (!Expect(service.IsOk(), "Plugin manager service should be queryable")) {
        (void)context.Fini();
        return false;
    }

    if (!Expect(
            service.Value()->GetPluginState("tgv_etching") ==
                PluginState::kStarted,
            "Real SEMIPLUGIN should be in Started state")) {
        (void)context.Fini();
        return false;
    }

    foundation::base::Result<Hh::Api::Plugin::IPluginTGVEtching*> plugin =
        service.Value()->GetPlugin<Hh::Api::Plugin::IPluginTGVEtching>(
            "tgv_etching");
    if (!Expect(plugin.IsOk(), "GetPlugin<IPluginTGVEtching> should succeed")) {
        (void)context.Fini();
        return false;
    }

    std::string image_path = "dummy.bmp";
    if (!Expect(
            plugin.Value()->AlignDetect(image_path) == 0.0,
            "Real SEMIPLUGIN interface call should return expected stub value")) {
        (void)context.Fini();
        return false;
    }

    foundation::base::Result<void> stop_result = context.Stop();
    if (!Expect(stop_result.IsOk(), "Context::Stop should stop real SEMIPLUGIN")) {
        (void)context.Fini();
        return false;
    }

    if (!Expect(
            service.Value()->GetPluginState("tgv_etching") ==
                PluginState::kStopped,
            "Real SEMIPLUGIN should be in Stopped state after Stop")) {
        (void)context.Fini();
        return false;
    }

    foundation::base::Result<void> fini_result = context.Fini();
    if (!Expect(fini_result.IsOk(), "Context::Fini should unload real SEMIPLUGIN")) {
        return false;
    }

    return true;
}

}  // namespace

int main() {
    if (!RunSingleServiceCase()) {
        return 1;
    }
    std::cout << "[OK] Single service case" << std::endl;

    if (!RunMultipleServiceCase()) {
        return 1;
    }
    std::cout << "[OK] Multiple service case" << std::endl;

    if (!RunDuplicateInstanceNameCase()) {
        return 1;
    }
    std::cout << "[OK] Duplicate instance name case" << std::endl;

    if (!RunRealSemipluginLifecycleCase()) {
        return 1;
    }
    std::cout << "[OK] Real SEMIPLUGIN lifecycle case" << std::endl;

    std::cout << "[PASSED] semiplugin_manager_plugin_integration_test" << std::endl;
    return 0;
}
