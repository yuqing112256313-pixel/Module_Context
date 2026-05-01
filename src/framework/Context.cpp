#include "framework/Context.h"

#include "framework/ModuleManager.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/base/Result.h"

namespace module_context {
namespace framework {

IContext& IContext::Instance() {
    static Context context;
    return context;
}

Context::Context()
    : module_manager_(new module_context::framework::ModuleManager()) {
}

Context::~Context() {
    (void)Fini();
}

foundation::base::Result<void> Context::Init() {
    if (!module_manager_) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidState,
            "Context::Init failed: module manager is unavailable");
    }

    return module_manager_->Init(*this);
}

foundation::base::Result<void> Context::Start() {
    if (!module_manager_) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidState,
            "Context::Start failed: module manager is unavailable");
    }

    return module_manager_->Start();
}

foundation::base::Result<void> Context::Stop() {
    if (!module_manager_) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidState,
            "Context::Stop failed: module manager is unavailable");
    }

    return module_manager_->Stop();
}

foundation::base::Result<void> Context::Fini() {
    if (!module_manager_) {
        return foundation::base::MakeSuccess();
    }

    return module_manager_->Fini();
}

IModuleManager* Context::ModuleManager() {
    return module_manager_.get();
}

foundation::base::Result<IModule*> Context::LookupServiceRaw(
    const char* service_key,
    const std::string& name) {
    if (!module_manager_) {
        return foundation::base::Result<IModule*>(
            foundation::base::ErrorCode::kInvalidState,
            "Context::GetService failed: module manager is unavailable");
    }

    return module_manager_->LookupNamedService(service_key, name);
}

foundation::base::Result<IModule*> Context::LookupUniqueServiceRaw(
    const char* service_key) {
    if (!module_manager_) {
        return foundation::base::Result<IModule*>(
            foundation::base::ErrorCode::kInvalidState,
            "Context::GetService failed: module manager is unavailable");
    }

    return module_manager_->LookupUniqueService(service_key);
}

}  // namespace framework
}  // namespace module_context
