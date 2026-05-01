#pragma once

#include "module_context/framework/IContext.h"

#include "foundation/base/NonCopyable.h"
#include "foundation/base/Result.h"

#include <memory>

namespace module_context {
namespace framework {

class ModuleManager;

class MC_FRAMEWORK_API Context final
    : public IContext,
      private foundation::base::NonCopyable {
public:
    Context();
    ~Context() override;

    foundation::base::Result<void> Init() override;
    foundation::base::Result<void> Start() override;
    foundation::base::Result<void> Stop() override;
    foundation::base::Result<void> Fini() override;

    IModuleManager* ModuleManager() override;

private:
    foundation::base::Result<IModule*> LookupServiceRaw(
        const char* service_key,
        const std::string& name) override;
    foundation::base::Result<IModule*> LookupUniqueServiceRaw(
        const char* service_key) override;

private:
    std::unique_ptr<module_context::framework::ModuleManager> module_manager_;
};

}  // namespace framework
}  // namespace module_context
