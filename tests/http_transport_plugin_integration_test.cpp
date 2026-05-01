#include "framework/Context.h"
#include "module_context/framework/IModuleManager.h"
#include "module_context/http/IHttpTransferService.h"

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

    const std::string instance_name = "http_transport_instance";
    foundation::base::Result<void> load_result =
        manager->LoadModule(instance_name, MC_TEST_HTTP_TRANSPORT_PLUGIN_PATH);
    if (!Expect(load_result.IsOk(), "LoadModule should load http_transport plugin")) {
        return false;
    }

    foundation::base::Result<module_context::framework::IModule*> module =
        manager->Module<module_context::framework::IModule>(instance_name);
    if (!Expect(module.IsOk(), "HTTP module should be queryable by instance name")) {
        return false;
    }
    if (!Expect(
            module.Value()->ModuleType() == "http_transport",
            "HTTP ModuleType should match plugin type")) {
        return false;
    }

    foundation::base::Result<module_context::http::IHttpTransferService*> named_http =
        context.GetService<module_context::http::IHttpTransferService>(instance_name);
    if (!Expect(named_http.IsOk(), "Named IHttpTransferService lookup should succeed")) {
        return false;
    }

    foundation::base::Result<module_context::http::IHttpTransferService*> unique_http =
        context.GetService<module_context::http::IHttpTransferService>();
    if (!Expect(unique_http.IsOk(), "Unique IHttpTransferService lookup should succeed")) {
        return false;
    }
    if (!Expect(
            named_http.Value() == unique_http.Value(),
            "Named and unique HTTP lookup should return the same instance")) {
        return false;
    }

    foundation::base::Result<void> fini_result = context.Fini();
    if (!Expect(fini_result.IsOk(), "Fini should unload http_transport plugin")) {
        return false;
    }

    foundation::base::Result<module_context::http::IHttpTransferService*> after_fini =
        context.GetService<module_context::http::IHttpTransferService>(instance_name);
    if (!Expect(
            !after_fini.IsOk() &&
                after_fini.GetError() == foundation::base::ErrorCode::kNotFound,
            "HTTP service lookup should return kNotFound after Fini")) {
        return false;
    }

    return true;
}

}  // namespace

int main() {
    if (!RunSingleServiceCase()) {
        return 1;
    }

    std::cout << "[PASSED] http_transport_plugin_integration_test" << std::endl;
    return 0;
}
