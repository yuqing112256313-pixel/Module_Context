/**
 * semiplugin_manager_unit_test.cpp
 *
 * 覆盖范围：
 *   1. PluginRegistry — Register / Lookup / Contains / Clear 及边界条件
 *   2. SemipluginManagerModule 元信息（ModuleType / ModuleVersion）
 *   3. SemipluginManagerModule 配置解析错误路径
 *      - 配置根不是对象
 *      - plugins 字段不是数组
 *      - 单条 plugin 缺少必填字段
 *      - 重复 plugin 名称
 *   4. SemipluginManagerModule DLL 加载失败路径
 *      - 指向不存在路径的 library 字段 → OnInit 返回错误，PluginState = kError
 *   5. GetPluginState 对未注册插件返回 kUnloaded
 *   6. LookupPluginRaw（通过 GetPlugin<T> 暴露）对未注册名称返回 kNotFound
 */

#include "SemipluginManagerModule.h"
#include "internal/PluginRegistry.h"

#include "module_context/framework/IContext.h"
#include "module_context/framework/IModuleManager.h"
#include "module_context/plugin/IPluginManagerService.h"
#include "module_context/plugin/Types.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/config/ConfigValue.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

using foundation::base::ErrorCode;
using foundation::config::ConfigValue;
using module_context::plugin::IPluginManagerService;
using module_context::plugin::PluginRegistry;
using module_context::plugin::PluginState;
using module_context::plugin::SemipluginManagerModule;

// ---------------------------------------------------------------------------
// 测试辅助
// ---------------------------------------------------------------------------

bool Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

bool ExpectError(
    const foundation::base::Result<void>& result,
    ErrorCode expected_code,
    const std::string& label) {
    if (!Expect(!result.IsOk(), label + " should fail")) {
        return false;
    }
    return Expect(
        result.GetError() == expected_code,
        label + " should return expected error code");
}

void SetField(ConfigValue* object,
              const std::string& key,
              const ConfigValue& value) {
    (void)object->Set(key, value);
}

void AppendValue(ConfigValue* array, const ConfigValue& value) {
    (void)array->Append(value);
}

// ---------------------------------------------------------------------------
// 构建合法 / 残缺的配置对象
// ---------------------------------------------------------------------------

ConfigValue MakePluginEntry(
    const std::string& name,
    const std::string& library    = "/nonexistent/plugin.dll",
    const std::string& create_fn  = "CreatePlugin",
    const std::string& destroy_fn = "DestroyPlugin") {
    ConfigValue entry = ConfigValue::MakeObject();
    SetField(&entry, "name", ConfigValue(name));
    SetField(&entry, "type", ConfigValue(name));
    SetField(&entry, "library", ConfigValue(library));
    SetField(&entry, "create_func", ConfigValue(create_fn));
    SetField(&entry, "destroy_func", ConfigValue(destroy_fn));
    return entry;
}

ConfigValue MakeValidConfig(const std::string& plugin_name = "test_plugin") {
    ConfigValue root = ConfigValue::MakeObject();
    ConfigValue plugins = ConfigValue::MakeArray();
    AppendValue(&plugins, MakePluginEntry(plugin_name));
    SetField(&root, "plugins", plugins);
    return root;
}

// ---------------------------------------------------------------------------
// 仿 ModuleManager：按模块实例名返回预设的 ConfigValue
// ---------------------------------------------------------------------------

class FakeModuleManager : public module_context::framework::IModuleManager {
public:
    explicit FakeModuleManager(
        const std::string& module_name,
        const ConfigValue& config)
        : module_name_(module_name),
          config_(config) {
    }

    foundation::base::Result<void> LoadModules(const std::string&) override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> LoadModule(
        const std::string&, const std::string&) override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> Init(
        module_context::framework::IContext&) override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> Start() override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> Stop() override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> Fini() override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<ConfigValue> ModuleConfig(
        const std::string& name) override {
        if (name != module_name_) {
            return foundation::base::Result<ConfigValue>(
                ErrorCode::kNotFound,
                "Unknown module: " + name);
        }
        return foundation::base::Result<ConfigValue>(config_);
    }

private:
    foundation::base::Result<module_context::framework::IModule*> LookupModuleRaw(
        const std::string&) override {
        return foundation::base::Result<module_context::framework::IModule*>(
            ErrorCode::kNotFound,
            "No test modules registered");
    }

    std::string module_name_;
    ConfigValue config_;
};

// ---------------------------------------------------------------------------
// 持有 FakeModuleManager 引用
// ---------------------------------------------------------------------------

class FakeContext : public module_context::framework::IContext {
public:
    explicit FakeContext(module_context::framework::IModuleManager* manager)
        : manager_(manager) {
    }

    foundation::base::Result<void> Init() override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> Start() override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> Stop() override {
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> Fini() override {
        return foundation::base::MakeSuccess();
    }

    module_context::framework::IModuleManager* ModuleManager() override {
        return manager_;
    }

private:
    foundation::base::Result<module_context::framework::IModule*> LookupServiceRaw(
        const char*,
        const std::string&) override {
        return foundation::base::Result<module_context::framework::IModule*>(
            ErrorCode::kNotFound,
            "No services registered in test context");
    }

    foundation::base::Result<module_context::framework::IModule*> LookupUniqueServiceRaw(
        const char*) override {
        return foundation::base::Result<module_context::framework::IModule*>(
            ErrorCode::kNotFound,
            "No services registered in test context");
    }

    module_context::framework::IModuleManager* manager_;
};

// ---------------------------------------------------------------------------
// 分组测试用例
// ---------------------------------------------------------------------------

// 1. PluginRegistry 单元测试
bool RunPluginRegistryTests() {
    {
        PluginRegistry registry;

        foundation::base::Result<Hh::Api::Plugin::IPlugin*> result =
            registry.Lookup("missing");
        if (!Expect(
                !result.IsOk() && result.GetError() == ErrorCode::kNotFound,
                "Lookup on empty registry should return kNotFound")) {
            return false;
        }

        if (!Expect(
                !registry.Contains("missing"),
                "Contains on empty registry should return false")) {
            return false;
        }
    }

    {
        PluginRegistry registry;

        // 用非空指针值模拟一个 IPlugin 实例地址（仅测试注册表逻辑，不解引用）
        Hh::Api::Plugin::IPlugin* fake_ptr =
            reinterpret_cast<Hh::Api::Plugin::IPlugin*>(static_cast<std::uintptr_t>(0x1));

        registry.Register("plugin_a", fake_ptr);

        if (!Expect(registry.Contains("plugin_a"),
                    "Contains should return true after Register")) {
            return false;
        }

        foundation::base::Result<Hh::Api::Plugin::IPlugin*> lookup =
            registry.Lookup("plugin_a");
        if (!Expect(lookup.IsOk(), "Lookup should succeed after Register")) {
            return false;
        }
        if (!Expect(lookup.Value() == fake_ptr,
                    "Lookup should return the same pointer that was registered")) {
            return false;
        }

        foundation::base::Result<Hh::Api::Plugin::IPlugin*> unknown =
            registry.Lookup("plugin_b");
        if (!Expect(
                !unknown.IsOk() && unknown.GetError() == ErrorCode::kNotFound,
                "Lookup of unknown name should return kNotFound")) {
            return false;
        }
    }

    {
        PluginRegistry registry;

        Hh::Api::Plugin::IPlugin* fake_ptr =
            reinterpret_cast<Hh::Api::Plugin::IPlugin*>(static_cast<std::uintptr_t>(0x2));

        registry.Register("plugin_x", fake_ptr);
        registry.Clear();

        if (!Expect(
                !registry.Contains("plugin_x"),
                "Contains should return false after Clear")) {
            return false;
        }

        foundation::base::Result<Hh::Api::Plugin::IPlugin*> after_clear =
            registry.Lookup("plugin_x");
        if (!Expect(
                !after_clear.IsOk() && after_clear.GetError() == ErrorCode::kNotFound,
                "Lookup should return kNotFound after Clear")) {
            return false;
        }
    }

    {
        PluginRegistry registry;
        Hh::Api::Plugin::IPlugin* fake_ptr =
            reinterpret_cast<Hh::Api::Plugin::IPlugin*>(static_cast<std::uintptr_t>(0x3));

        // NULL 指针注册应被静默忽略
        registry.Register("plugin_null", NULL);
        if (!Expect(
                !registry.Contains("plugin_null"),
                "Register with NULL instance should be ignored")) {
            return false;
        }

        // 空名称注册应被静默忽略
        registry.Register("", fake_ptr);
        if (!Expect(
                !registry.Contains(""),
                "Register with empty name should be ignored")) {
            return false;
        }
    }

    return true;
}

// 2. 模块元信息测试
bool RunModuleMetaTests() {
    SemipluginManagerModule module;

    if (!Expect(module.ModuleType() == "semiplugin_manager",
                "ModuleType should return 'semiplugin_manager'")) {
        return false;
    }

    if (!Expect(!module.ModuleVersion().empty(),
                "ModuleVersion should not be empty")) {
        return false;
    }

    if (!Expect(
            module.GetPluginState("any_name") == PluginState::kUnloaded,
            "GetPluginState for unregistered plugin should return kUnloaded")) {
        return false;
    }

    return true;
}

// 3. 配置解析错误路径
bool RunConfigParseErrorTests() {
    const std::string instance_name = "semiplugin_manager";

    {
        // 配置根不是对象（是一个字符串）
        FakeModuleManager mgr(instance_name, ConfigValue("not_an_object"));
        FakeContext ctx(&mgr);
        SemipluginManagerModule module;
        (void)module.SetModuleName(instance_name);

        foundation::base::Result<void> result = module.Init(ctx);
        if (!ExpectError(result, ErrorCode::kParseError,
                         "OnInit with non-object config")) {
            return false;
        }
    }

    {
        // plugins 字段是字符串而不是数组
        ConfigValue bad_config = ConfigValue::MakeObject();
        SetField(&bad_config, "plugins", ConfigValue("not_an_array"));
        FakeModuleManager mgr(instance_name, bad_config);
        FakeContext ctx(&mgr);
        SemipluginManagerModule module;
        (void)module.SetModuleName(instance_name);

        foundation::base::Result<void> result = module.Init(ctx);
        if (!ExpectError(result, ErrorCode::kParseError,
                         "OnInit with plugins as non-array")) {
            return false;
        }
    }

    {
        // 缺少必填字段 library
        ConfigValue entry = ConfigValue::MakeObject();
        SetField(&entry, "name", ConfigValue("tgv_etching"));
        SetField(&entry, "create_func", ConfigValue("CreatePlugin"));
        SetField(&entry, "destroy_func", ConfigValue("DestroyPlugin"));
        // 故意不设 library

        ConfigValue root = ConfigValue::MakeObject();
        ConfigValue plugins = ConfigValue::MakeArray();
        AppendValue(&plugins, entry);
        SetField(&root, "plugins", plugins);

        FakeModuleManager mgr(instance_name, root);
        FakeContext ctx(&mgr);
        SemipluginManagerModule module;
        (void)module.SetModuleName(instance_name);

        foundation::base::Result<void> result = module.Init(ctx);
        if (!ExpectError(result, ErrorCode::kParseError,
                         "OnInit with missing library field")) {
            return false;
        }
    }

    {
        // 缺少必填字段 create_func
        ConfigValue entry = ConfigValue::MakeObject();
        SetField(&entry, "name", ConfigValue("tgv_etching"));
        SetField(&entry, "library", ConfigValue("/some/path.dll"));
        SetField(&entry, "destroy_func", ConfigValue("DestroyPlugin"));
        // 故意不设 create_func

        ConfigValue root = ConfigValue::MakeObject();
        ConfigValue plugins = ConfigValue::MakeArray();
        AppendValue(&plugins, entry);
        SetField(&root, "plugins", plugins);

        FakeModuleManager mgr(instance_name, root);
        FakeContext ctx(&mgr);
        SemipluginManagerModule module;
        (void)module.SetModuleName(instance_name);

        foundation::base::Result<void> result = module.Init(ctx);
        if (!ExpectError(result, ErrorCode::kParseError,
                         "OnInit with missing create_func field")) {
            return false;
        }
    }

    {
        // 可选字段 type 如果出现，必须是字符串
        ConfigValue entry = MakePluginEntry("bad_type_plugin");
        SetField(&entry, "type", ConfigValue::MakeArray());

        ConfigValue root = ConfigValue::MakeObject();
        ConfigValue plugins = ConfigValue::MakeArray();
        AppendValue(&plugins, entry);
        SetField(&root, "plugins", plugins);

        FakeModuleManager mgr(instance_name, root);
        FakeContext ctx(&mgr);
        SemipluginManagerModule module;
        (void)module.SetModuleName(instance_name);

        foundation::base::Result<void> result = module.Init(ctx);
        if (!ExpectError(result, ErrorCode::kParseError,
                         "OnInit with non-string type field")) {
            return false;
        }
    }

    {
        // 可选字段 type 如果出现，不能是空字符串
        ConfigValue entry = MakePluginEntry("empty_type_plugin");
        SetField(&entry, "type", ConfigValue(""));

        ConfigValue root = ConfigValue::MakeObject();
        ConfigValue plugins = ConfigValue::MakeArray();
        AppendValue(&plugins, entry);
        SetField(&root, "plugins", plugins);

        FakeModuleManager mgr(instance_name, root);
        FakeContext ctx(&mgr);
        SemipluginManagerModule module;
        (void)module.SetModuleName(instance_name);

        foundation::base::Result<void> result = module.Init(ctx);
        if (!ExpectError(result, ErrorCode::kParseError,
                         "OnInit with empty type field")) {
            return false;
        }
    }

    {
        // 重复插件名称
        ConfigValue root = ConfigValue::MakeObject();
        ConfigValue plugins = ConfigValue::MakeArray();
        AppendValue(&plugins, MakePluginEntry("dup_plugin"));
        AppendValue(&plugins, MakePluginEntry("dup_plugin"));  // 与第一条同名
        SetField(&root, "plugins", plugins);

        FakeModuleManager mgr(instance_name, root);
        FakeContext ctx(&mgr);
        SemipluginManagerModule module;
        (void)module.SetModuleName(instance_name);

        foundation::base::Result<void> result = module.Init(ctx);
        if (!ExpectError(result, ErrorCode::kAlreadyExists,
                         "OnInit with duplicate plugin names")) {
            return false;
        }
    }

    {
        // 空 plugins 数组 → 应当成功（0 个插件是合法配置）
        ConfigValue root = ConfigValue::MakeObject();
        ConfigValue plugins = ConfigValue::MakeArray();
        SetField(&root, "plugins", plugins);

        FakeModuleManager mgr(instance_name, root);
        FakeContext ctx(&mgr);
        SemipluginManagerModule module;
        (void)module.SetModuleName(instance_name);

        foundation::base::Result<void> result = module.Init(ctx);
        if (!Expect(result.IsOk(),
                    "OnInit with empty plugins array should succeed")) {
            return false;
        }

        (void)module.Fini();
    }

    return true;
}

// 4. DLL 加载失败路径（library 指向不存在的路径）
bool RunDllLoadFailureTests() {
    const std::string instance_name = "semiplugin_manager";

    {
        ConfigValue root = MakeValidConfig("tgv_etching");
        FakeModuleManager mgr(instance_name, root);
        FakeContext ctx(&mgr);
        SemipluginManagerModule module;
        (void)module.SetModuleName(instance_name);

        foundation::base::Result<void> result = module.Init(ctx);
        if (!Expect(!result.IsOk(),
                    "OnInit should fail when library path does not exist")) {
            return false;
        }

        if (!Expect(
                result.GetError() == ErrorCode::kIoError ||
                    result.GetError() == ErrorCode::kOperationFailed,
                "OnInit should return kIoError or kOperationFailed for missing DLL")) {
            return false;
        }

        if (!Expect(
                module.GetPluginState("tgv_etching") == PluginState::kError,
                "Plugin state should be kError after failed DLL load")) {
            return false;
        }
    }

    return true;
}

// 5. GetPlugin<T> 对未注册名称返回 kNotFound
bool RunGetPluginUnknownNameTests() {
    const std::string instance_name = "semiplugin_manager";

    // 加载 0 个插件的模块，验证 GetPlugin 对未知名称报 kNotFound
    ConfigValue root = ConfigValue::MakeObject();
    ConfigValue plugins = ConfigValue::MakeArray();
    SetField(&root, "plugins", plugins);

    FakeModuleManager mgr(instance_name, root);
    FakeContext ctx(&mgr);
    SemipluginManagerModule module;
    (void)module.SetModuleName(instance_name);
    (void)module.Init(ctx);

    // IPluginManagerService 接口的 GetPlugin<T> 是模板，
    // 这里借助 dynamic_cast 不存在的基类作为 T 来触发 kNotFound 分支。
    // 实际类型用 module_context::framework::IModule 代替（不是 IPlugin 子类）。
    IPluginManagerService* svc = &module;
    foundation::base::Result<module_context::framework::IModule*> not_found =
        svc->GetPlugin<module_context::framework::IModule>("ghost");
    if (!Expect(
            !not_found.IsOk() && not_found.GetError() == ErrorCode::kNotFound,
            "GetPlugin for unregistered name should return kNotFound")) {
        (void)module.Fini();
        return false;
    }

    (void)module.Fini();
    return true;
}

// 6. ModuleManager 不可用时 OnInit 报 kInvalidState
bool RunNullManagerTests() {
    const std::string instance_name = "semiplugin_manager";

    FakeContext ctx(NULL);  // manager 为 NULL
    SemipluginManagerModule module;
    (void)module.SetModuleName(instance_name);

    foundation::base::Result<void> result = module.Init(ctx);
    if (!ExpectError(result, ErrorCode::kInvalidState,
                     "OnInit with null ModuleManager")) {
        return false;
    }

    return true;
}

}  // namespace

int main() {
    if (!RunPluginRegistryTests()) {
        return 1;
    }
    std::cout << "[OK] PluginRegistry tests" << std::endl;

    if (!RunModuleMetaTests()) {
        return 1;
    }
    std::cout << "[OK] Module meta tests" << std::endl;

    if (!RunConfigParseErrorTests()) {
        return 1;
    }
    std::cout << "[OK] Config parse error tests" << std::endl;

    if (!RunDllLoadFailureTests()) {
        return 1;
    }
    std::cout << "[OK] DLL load failure tests" << std::endl;

    if (!RunGetPluginUnknownNameTests()) {
        return 1;
    }
    std::cout << "[OK] GetPlugin unknown name tests" << std::endl;

    if (!RunNullManagerTests()) {
        return 1;
    }
    std::cout << "[OK] Null manager tests" << std::endl;

    std::cout << "[PASSED] semiplugin_manager_unit_test" << std::endl;
    return 0;
}
