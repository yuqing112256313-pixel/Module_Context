#pragma once

#include "module_context/framework/IModule.h"

#include "foundation/base/NonCopyable.h"
#include "foundation/base/Result.h"

#include <string>
#include <unordered_map>

namespace module_context {
namespace framework {

class ServiceRegistry : private foundation::base::NonCopyable {
public:
    ServiceRegistry();
    ~ServiceRegistry();

    void Register(
        const std::string& service_key,
        const std::string& name,
        IModule* provider);
    void RegisterKnownServices(
        const std::string& name,
        IModule* provider);
    foundation::base::Result<IModule*> Lookup(
        const std::string& service_key,
        const std::string& name) const;
    foundation::base::Result<IModule*> LookupUnique(
        const std::string& service_key) const;
    void Clear();

private:
    typedef std::unordered_map<std::string, IModule*> ServiceMap;
    typedef std::unordered_map<std::string, ServiceMap> ServicesByKey;

    ServicesByKey services_by_key_;
};

}  // namespace framework
}  // namespace module_context
