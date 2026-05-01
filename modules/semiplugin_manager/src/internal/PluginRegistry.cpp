#include "PluginRegistry.h"

#include "foundation/base/ErrorCode.h"

#include "iplugin.h"

namespace module_context {
namespace plugin {

PluginRegistry::PluginRegistry()
    : plugins_() {
}

PluginRegistry::~PluginRegistry() {
}

void PluginRegistry::Register(
    const std::string& name,
    Hh::Api::Plugin::IPlugin* instance) {
    if (name.empty() || instance == NULL) {
        return;
    }

    plugins_[name] = instance;
}

foundation::base::Result<Hh::Api::Plugin::IPlugin*> PluginRegistry::Lookup(
    const std::string& name) const {
    if (name.empty()) {
        return foundation::base::Result<Hh::Api::Plugin::IPlugin*>(
            foundation::base::ErrorCode::kInvalidArgument,
            "PluginRegistry::Lookup failed: plugin name is empty");
    }

    PluginMap::const_iterator it = plugins_.find(name);
    if (it == plugins_.end() || it->second == NULL) {
        return foundation::base::Result<Hh::Api::Plugin::IPlugin*>(
            foundation::base::ErrorCode::kNotFound,
            "PluginRegistry::Lookup failed: plugin '" + name + "' is not registered");
    }

    return foundation::base::Result<Hh::Api::Plugin::IPlugin*>(it->second);
}

bool PluginRegistry::Contains(const std::string& name) const {
    return plugins_.find(name) != plugins_.end();
}

void PluginRegistry::Clear() {
    plugins_.clear();
}

}  // namespace plugin
}  // namespace module_context
