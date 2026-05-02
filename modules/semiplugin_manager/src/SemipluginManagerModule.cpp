#include "SemipluginManagerModule.h"

#include "internal/ConfigTypes.h"
#include "internal/PluginLoader.h"
#include "internal/PluginRegistry.h"

#include "module_context/framework/IModuleManager.h"

#include "module_context/plugin/ModuleFactory.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/log/Logger.h"

#include "iplugin.h"

#include <string>
#include <vector>

namespace module_context {
namespace plugin {

namespace {

// ---------------------------------------------------------------------------
// 配置解析辅助函数
// ---------------------------------------------------------------------------

foundation::base::Result<std::string> GetRequiredStringField(
    const foundation::config::ConfigValue& root,
    const std::string& key) {
    if (!root.IsObject()) {
        return foundation::base::Result<std::string>(
            foundation::base::ErrorCode::kParseError,
            "Expected configuration object when reading '" + key + "'");
    }

    foundation::base::Result<foundation::config::ConfigValue> value =
        root.ObjectGet(key);
    if (!value.IsOk()) {
        return foundation::base::Result<std::string>(
            foundation::base::ErrorCode::kParseError,
            "Missing required string field '" + key + "'");
    }

    foundation::base::Result<std::string> string_value = value.Value().AsString();
    if (!string_value.IsOk() || string_value.Value().empty()) {
        return foundation::base::Result<std::string>(
            foundation::base::ErrorCode::kParseError,
            "Field '" + key + "' must be a non-empty string");
    }

    return string_value;
}

foundation::base::Result<std::string> GetOptionalStringField(
    const foundation::config::ConfigValue& root,
    const std::string& key,
    const std::string& fallback) {
    if (!root.IsObject() || !root.Contains(key)) {
        return foundation::base::Result<std::string>(fallback);
    }

    foundation::base::Result<foundation::config::ConfigValue> value =
        root.ObjectGet(key);
    if (!value.IsOk()) {
        return foundation::base::Result<std::string>(
            value.GetError(),
            value.GetMessage());
    }

    foundation::base::Result<std::string> string_value = value.Value().AsString();
    if (!string_value.IsOk()) {
        return foundation::base::Result<std::string>(
            foundation::base::ErrorCode::kParseError,
            "Field '" + key + "' must be a string");
    }

    return string_value;
}

/**
 * @brief 解析单个插件条目配置对象。
 *
 * 期望格式：
 * @code{.json}
 * {
 *   "name":         "tgv_etching",
 *   "type":         "tgv_etching",
 *   "library":      "path/to/YGV.dll",
 *   "create_func":  "CreatePluginEtching",
 *   "destroy_func": "DestroyPluginEtching"
 * }
 * @endcode
 */
foundation::base::Result<PluginEntryConfig> ParsePluginEntryConfig(
    const foundation::config::ConfigValue& value) {
    foundation::base::Result<std::string> name =
        GetRequiredStringField(value, "name");
    if (!name.IsOk()) {
        return foundation::base::Result<PluginEntryConfig>(
            name.GetError(),
            name.GetMessage());
    }

    foundation::base::Result<std::string> type =
        GetOptionalStringField(value, "type", name.Value());
    if (!type.IsOk()) {
        return foundation::base::Result<PluginEntryConfig>(
            type.GetError(),
            type.GetMessage());
    }
    if (type.Value().empty()) {
        return foundation::base::Result<PluginEntryConfig>(
            foundation::base::ErrorCode::kParseError,
            "Field 'type' must be a non-empty string when present");
    }

    foundation::base::Result<std::string> library =
        GetRequiredStringField(value, "library");
    if (!library.IsOk()) {
        return foundation::base::Result<PluginEntryConfig>(
            library.GetError(),
            library.GetMessage());
    }

    foundation::base::Result<std::string> create_func =
        GetRequiredStringField(value, "create_func");
    if (!create_func.IsOk()) {
        return foundation::base::Result<PluginEntryConfig>(
            create_func.GetError(),
            create_func.GetMessage());
    }

    foundation::base::Result<std::string> destroy_func =
        GetRequiredStringField(value, "destroy_func");
    if (!destroy_func.IsOk()) {
        return foundation::base::Result<PluginEntryConfig>(
            destroy_func.GetError(),
            destroy_func.GetMessage());
    }

    PluginEntryConfig config;
    config.name = name.Value();
    config.type = type.Value();
    config.library_path = library.Value();
    config.create_func = create_func.Value();
    config.destroy_func = destroy_func.Value();
    return foundation::base::Result<PluginEntryConfig>(config);
}

/**
 * @brief 解析工序插件管理模块的完整配置。
 *
 * 期望根对象包含 "plugins" 数组，每个元素由 ParsePluginEntryConfig 解析。
 * 同名插件实例会被视为重复项并报错。
 */
foundation::base::Result<SemipluginManagerConfig> ParseManagerConfig(
    const foundation::config::ConfigValue& config_value) {
    if (!config_value.IsObject()) {
        return foundation::base::Result<SemipluginManagerConfig>(
            foundation::base::ErrorCode::kParseError,
            "SemipluginManager config must be an object");
    }

    if (!config_value.IsObject() || !config_value.Contains("plugins")) {
        return foundation::base::Result<SemipluginManagerConfig>(
            SemipluginManagerConfig());
    }

    foundation::base::Result<foundation::config::ConfigValue> plugins_field =
        config_value.ObjectGet("plugins");
    if (!plugins_field.IsOk()) {
        return foundation::base::Result<SemipluginManagerConfig>(
            plugins_field.GetError(),
            plugins_field.GetMessage());
    }

    if (!plugins_field.Value().IsArray()) {
        return foundation::base::Result<SemipluginManagerConfig>(
            foundation::base::ErrorCode::kParseError,
            "Field 'plugins' must be an array");
    }

    foundation::base::Result<foundation::config::ConfigValue::Array> plugins_array =
        plugins_field.Value().AsArray();
    if (!plugins_array.IsOk()) {
        return foundation::base::Result<SemipluginManagerConfig>(
            plugins_array.GetError(),
            plugins_array.GetMessage());
    }

    SemipluginManagerConfig config;
    const foundation::config::ConfigValue::Array& entries = plugins_array.Value();

    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (!entries[index].IsObject()) {
            return foundation::base::Result<SemipluginManagerConfig>(
                foundation::base::ErrorCode::kParseError,
                "plugins[" +
                    std::to_string(static_cast<unsigned long long>(index)) +
                    "] must be an object");
        }

        foundation::base::Result<PluginEntryConfig> entry =
            ParsePluginEntryConfig(entries[index]);
        if (!entry.IsOk()) {
            return foundation::base::Result<SemipluginManagerConfig>(
                entry.GetError(),
                "Failed to parse plugins[" +
                    std::to_string(static_cast<unsigned long long>(index)) +
                    "]: " + entry.GetMessage());
        }

        for (std::size_t prev = 0; prev < config.plugins.size(); ++prev) {
            if (config.plugins[prev].name == entry.Value().name) {
                return foundation::base::Result<SemipluginManagerConfig>(
                    foundation::base::ErrorCode::kAlreadyExists,
                    "Duplicate plugin name '" + entry.Value().name + "' in plugins array");
            }
        }

        config.plugins.push_back(entry.Value());
    }

    return foundation::base::Result<SemipluginManagerConfig>(config);
}

}  // namespace

// ---------------------------------------------------------------------------
// SemipluginManagerModule
// ---------------------------------------------------------------------------

SemipluginManagerModule::SemipluginManagerModule()
    : loader_(new PluginLoader()),
      registry_(new PluginRegistry()),
      plugin_load_order_(),
      loaded_plugins_(),
      plugin_states_() {
}

SemipluginManagerModule::~SemipluginManagerModule() {
}

std::string SemipluginManagerModule::ModuleType() const {
    return "semiplugin_manager";
}

std::string SemipluginManagerModule::ModuleVersion() const {
    return "1.0.0";
}

PluginState SemipluginManagerModule::GetPluginState(const std::string& name) const {
    std::map<std::string, PluginState>::const_iterator it =
        plugin_states_.find(name);
    if (it == plugin_states_.end()) {
        return PluginState::kUnloaded;
    }
    return it->second;
}

foundation::base::Result<Hh::Api::Plugin::IPlugin*>
SemipluginManagerModule::LookupPluginRaw(const std::string& name) {
    return registry_->Lookup(name);
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

foundation::base::Result<void> SemipluginManagerModule::OnInit() {
    module_context::framework::IModuleManager* manager = Context().ModuleManager();
    if (manager == NULL) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidState,
            "Module manager is unavailable");
    }

    foundation::base::Result<foundation::config::ConfigValue> config =
        manager->ModuleConfig(ModuleName());
    if (!config.IsOk()) {
        return foundation::base::Result<void>(
            config.GetError(),
            config.GetMessage());
    }

    foundation::base::Result<SemipluginManagerConfig> parsed =
        ParseManagerConfig(config.Value());
    if (!parsed.IsOk()) {
        return foundation::base::Result<void>(
            parsed.GetError(),
            parsed.GetMessage());
    }

    const SemipluginManagerConfig& manager_config = parsed.Value();
    for (std::size_t index = 0; index < manager_config.plugins.size(); ++index) {
        foundation::base::Result<void> load_result =
            LoadAndInitPlugin(manager_config.plugins[index]);
        if (!load_result.IsOk()) {
            (void)OnFini();
            return load_result;
        }
    }

    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> SemipluginManagerModule::LoadAndInitPlugin(
    const PluginEntryConfig& entry_config) {
    if (registry_->Contains(entry_config.name)) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kAlreadyExists,
            "Plugin '" + entry_config.name + "' is already loaded");
    }

    foundation::base::Result<LoadedPlugin> load_result =
        loader_->Load(entry_config);
    if (!load_result.IsOk()) {
        plugin_states_[entry_config.name] = PluginState::kError;
        return foundation::base::Result<void>(
            load_result.GetError(),
            load_result.GetMessage());
    }

    LoadedPlugin loaded = load_result.Value();
    plugin_states_[entry_config.name] = PluginState::kLoaded;

    bool init_ok = false;
    try {
        init_ok = loaded.instance->Init();
    } catch (...) {
        loader_->Unload(&loaded);
        plugin_states_[entry_config.name] = PluginState::kError;
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kOperationFailed,
            "Exception thrown by Init() for plugin '" + entry_config.name + "'");
    }

    if (!init_ok) {
        loader_->Unload(&loaded);
        plugin_states_[entry_config.name] = PluginState::kError;
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kOperationFailed,
            "Plugin Init() returned false for '" + entry_config.name + "'");
    }

    plugin_states_[entry_config.name] = PluginState::kInitialized;
    loaded_plugins_[entry_config.name] = loaded;
    registry_->Register(entry_config.name, loaded.instance);
    plugin_load_order_.push_back(entry_config.name);

    FOUNDATION_LOG_INFO(
        "[SemipluginManager] Plugin '" << entry_config.name << "' loaded and initialized");
    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> SemipluginManagerModule::OnStart() {
    std::vector<std::string> started_plugins;

    for (std::size_t index = 0; index < plugin_load_order_.size(); ++index) {
        const std::string& name = plugin_load_order_[index];

        std::map<std::string, LoadedPlugin>::iterator it =
            loaded_plugins_.find(name);
        if (it == loaded_plugins_.end() || it->second.instance == NULL) {
            continue;
        }

        bool start_ok = false;
        try {
            start_ok = it->second.instance->Start();
        } catch (...) {
            plugin_states_[name] = PluginState::kError;
            (void)StopPluginsInReverse(started_plugins);
            return foundation::base::Result<void>(
                foundation::base::ErrorCode::kOperationFailed,
                "Exception thrown by Start() for plugin '" + name + "'");
        }

        if (!start_ok) {
            plugin_states_[name] = PluginState::kError;
            (void)StopPluginsInReverse(started_plugins);
            return foundation::base::Result<void>(
                foundation::base::ErrorCode::kOperationFailed,
                "Plugin Start() returned false for '" + name + "'");
        }

        plugin_states_[name] = PluginState::kStarted;
        started_plugins.push_back(name);
        FOUNDATION_LOG_INFO("[SemipluginManager] Plugin '" << name << "' started");
    }

    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> SemipluginManagerModule::StopPluginsInReverse(
    const std::vector<std::string>& plugin_names) {
    foundation::base::Result<void> first_error = foundation::base::MakeSuccess();

    for (int index = static_cast<int>(plugin_names.size()) - 1;
         index >= 0;
         --index) {
        const std::string& name =
            plugin_names[static_cast<std::size_t>(index)];

        std::map<std::string, LoadedPlugin>::iterator it =
            loaded_plugins_.find(name);
        if (it == loaded_plugins_.end() || it->second.instance == NULL) {
            continue;
        }

        if (GetPluginState(name) != PluginState::kStarted) {
            continue;
        }

        bool stop_ok = false;
        try {
            stop_ok = it->second.instance->Stop();
        } catch (...) {
            plugin_states_[name] = PluginState::kError;
            if (first_error.IsOk()) {
                first_error = foundation::base::Result<void>(
                    foundation::base::ErrorCode::kOperationFailed,
                    "Exception thrown by Stop() for plugin '" + name + "'");
            }
            continue;
        }

        if (!stop_ok) {
            plugin_states_[name] = PluginState::kError;
            if (first_error.IsOk()) {
                first_error = foundation::base::Result<void>(
                    foundation::base::ErrorCode::kOperationFailed,
                    "Plugin Stop() returned false for '" + name + "'");
            }
            continue;
        }

        plugin_states_[name] = PluginState::kStopped;
        FOUNDATION_LOG_INFO("[SemipluginManager] Plugin '" << name << "' stopped");
    }

    return first_error;
}

foundation::base::Result<void> SemipluginManagerModule::OnStop() {
    return StopPluginsInReverse(plugin_load_order_);
}

foundation::base::Result<void> SemipluginManagerModule::OnFini() {
    foundation::base::Result<void> first_error = foundation::base::MakeSuccess();

    for (int index = static_cast<int>(plugin_load_order_.size()) - 1;
         index >= 0;
         --index) {
        const std::string& name =
            plugin_load_order_[static_cast<std::size_t>(index)];

        std::map<std::string, LoadedPlugin>::iterator it =
            loaded_plugins_.find(name);
        if (it == loaded_plugins_.end()) {
            continue;
        }

        if (it->second.instance != NULL) {
            try {
                it->second.instance->Fini();
            } catch (...) {
                if (first_error.IsOk()) {
                    first_error = foundation::base::Result<void>(
                        foundation::base::ErrorCode::kOperationFailed,
                        "Exception thrown by Fini() for plugin '" + name + "'");
                }
            }
        }

        loader_->Unload(&it->second);
        plugin_states_[name] = PluginState::kUnloaded;
        FOUNDATION_LOG_INFO("[SemipluginManager] Plugin '" << name << "' unloaded");
    }

    registry_->Clear();
    plugin_load_order_.clear();
    loaded_plugins_.clear();

    return first_error;
}

MC_DECLARE_MODULE_FACTORY(SemipluginManagerModule)

}  // namespace plugin
}  // namespace module_context
