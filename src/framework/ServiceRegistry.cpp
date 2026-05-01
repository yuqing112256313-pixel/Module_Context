#include "framework/ServiceRegistry.h"

#include "module_context/http/IHttpTransferService.h"
#include "module_context/messaging/IMessageBusService.h"
#include "module_context/plugin/IPluginManagerService.h"

#include "foundation/base/ErrorCode.h"

namespace module_context {
namespace framework {

ServiceRegistry::ServiceRegistry()
    : services_by_key_() {
}

ServiceRegistry::~ServiceRegistry() {
}

void ServiceRegistry::Register(
    const std::string& service_key,
    const std::string& name,
    IModule* provider) {
    if (service_key.empty() || name.empty() || provider == NULL) {
        return;
    }

    services_by_key_[service_key][name] = provider;
}

void ServiceRegistry::RegisterKnownServices(
    const std::string& name,
    IModule* provider) {
    if (provider == NULL) {
        return;
    }

    // 当前框架采用显式白名单登记服务接口。新增公开服务接口时，应在这里补充
    // dynamic_cast 分支，并在对应公开头文件中提供 ServiceTypeTraits 特化。
    if (dynamic_cast<module_context::messaging::IMessageBusService*>(provider) != NULL) {
        Register(
            ServiceTypeTraits<module_context::messaging::IMessageBusService>::Key(),
            name,
            provider);
    }

    if (dynamic_cast<module_context::http::IHttpTransferService*>(provider) != NULL) {
        Register(
            ServiceTypeTraits<module_context::http::IHttpTransferService>::Key(),
            name,
            provider);
    }

    if (dynamic_cast<module_context::plugin::IPluginManagerService*>(provider) != NULL) {
        Register(
            ServiceTypeTraits<module_context::plugin::IPluginManagerService>::Key(),
            name,
            provider);
    }
}

foundation::base::Result<IModule*> ServiceRegistry::Lookup(
    const std::string& service_key,
    const std::string& name) const {
    if (service_key.empty() || name.empty()) {
        return foundation::base::Result<IModule*>(
            foundation::base::ErrorCode::kInvalidArgument,
            "ServiceRegistry::Lookup failed: service key or name is empty");
    }

    ServicesByKey::const_iterator by_key = services_by_key_.find(service_key);
    if (by_key == services_by_key_.end()) {
        return foundation::base::Result<IModule*>(
            foundation::base::ErrorCode::kNotFound,
            "ServiceRegistry::Lookup failed: service key '" + service_key +
                "' has no provider named '" + name + "'");
    }

    ServiceMap::const_iterator by_name = by_key->second.find(name);
    if (by_name == by_key->second.end()) {
        return foundation::base::Result<IModule*>(
            foundation::base::ErrorCode::kNotFound,
            "ServiceRegistry::Lookup failed: service key '" + service_key +
                "' has no provider named '" + name + "'");
    }

    if (by_name->second == NULL) {
        return foundation::base::Result<IModule*>(
            foundation::base::ErrorCode::kInvalidState,
            "ServiceRegistry::Lookup failed: service '" + name +
                "' is not available");
    }

    return foundation::base::Result<IModule*>(by_name->second);
}

foundation::base::Result<IModule*> ServiceRegistry::LookupUnique(
    const std::string& service_key) const {
    if (service_key.empty()) {
        return foundation::base::Result<IModule*>(
            foundation::base::ErrorCode::kInvalidArgument,
            "ServiceRegistry::LookupUnique failed: service key is empty");
    }

    ServicesByKey::const_iterator by_key = services_by_key_.find(service_key);
    if (by_key == services_by_key_.end() || by_key->second.empty()) {
        return foundation::base::Result<IModule*>(
            foundation::base::ErrorCode::kNotFound,
            "ServiceRegistry::LookupUnique failed: service was not found");
    }

    if (by_key->second.size() != 1) {
        return foundation::base::Result<IModule*>(
            foundation::base::ErrorCode::kInvalidState,
            "ServiceRegistry::LookupUnique failed: multiple services are registered");
    }

    IModule* provider = by_key->second.begin()->second;
    if (provider == NULL) {
        return foundation::base::Result<IModule*>(
            foundation::base::ErrorCode::kInvalidState,
            "ServiceRegistry::LookupUnique failed: service is not available");
    }

    return foundation::base::Result<IModule*>(provider);
}

void ServiceRegistry::Clear() {
    services_by_key_.clear();
}

}  // namespace framework
}  // namespace module_context
