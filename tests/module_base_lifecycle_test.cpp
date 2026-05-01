#include "framework/ModuleBase.h"

#include "module_context/framework/IModuleManager.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/base/Result.h"

#include <iostream>
#include <string>

using module_context::framework::ModuleState;

namespace {

class DummyContext : public module_context::framework::IContext {
public:
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
        return NULL;
    }

private:
    foundation::base::Result<module_context::framework::IModule*> LookupServiceRaw(
        const char*,
        const std::string&) override {
        return foundation::base::Result<module_context::framework::IModule*>(
            foundation::base::ErrorCode::kNotFound,
            "No services are registered");
    }

    foundation::base::Result<module_context::framework::IModule*> LookupUniqueServiceRaw(
        const char*) override {
        return foundation::base::Result<module_context::framework::IModule*>(
            foundation::base::ErrorCode::kNotFound,
            "No services are registered");
    }
};

class TestModule : public module_context::framework::ModuleBase {
public:
    TestModule()
        : on_init_calls_(0),
          on_start_calls_(0),
          on_stop_calls_(0),
          on_fini_calls_(0) {
    }

    std::string ModuleType() const override {
        return "test-module";
    }

    std::string ModuleVersion() const override {
        return "1.0.0";
    }

    int InitCalls() const { return on_init_calls_; }
    int StartCalls() const { return on_start_calls_; }
    int StopCalls() const { return on_stop_calls_; }
    int FiniCalls() const { return on_fini_calls_; }

protected:
    foundation::base::Result<void> OnInit() override {
        ++on_init_calls_;
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> OnStart() override {
        ++on_start_calls_;
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> OnStop() override {
        ++on_stop_calls_;
        return foundation::base::MakeSuccess();
    }

    foundation::base::Result<void> OnFini() override {
        ++on_fini_calls_;
        return foundation::base::MakeSuccess();
    }

private:
    int on_init_calls_;
    int on_start_calls_;
    int on_stop_calls_;
    int on_fini_calls_;
};

class StopFailingModule : public module_context::framework::ModuleBase {
public:
    StopFailingModule()
        : on_stop_calls_(0),
          on_fini_calls_(0) {
    }

    std::string ModuleType() const override {
        return "stop-failing-module";
    }

    std::string ModuleVersion() const override {
        return "1.0.0";
    }

    int StopCalls() const { return on_stop_calls_; }
    int FiniCalls() const { return on_fini_calls_; }

protected:
    foundation::base::Result<void> OnStop() override {
        ++on_stop_calls_;
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidState,
            "stop hook failed");
    }

    foundation::base::Result<void> OnFini() override {
        ++on_fini_calls_;
        return foundation::base::MakeSuccess();
    }

private:
    int on_stop_calls_;
    int on_fini_calls_;
};

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }

    return true;
}

bool ExpectInvalidStateResult(
    const foundation::base::Result<void>& result,
    const char* failure_message) {
    if (!Expect(!result.IsOk(), failure_message)) {
        return false;
    }

    return Expect(
        result.GetError() == foundation::base::ErrorCode::kInvalidState,
        "invalid lifecycle transition should return kInvalidState");
}

}  // namespace

int main() {
    DummyContext ctx;

    {
        TestModule module;

        if (!Expect(module.ModuleName() == "test-module",
                    "ModuleName should fall back to ModuleType before injection")) {
            return 1;
        }

        foundation::base::Result<void> set_name_result =
            module.SetModuleName("test-instance");
        if (!Expect(set_name_result.IsOk(),
                    "SetModuleName should succeed in Created state")) {
            return 1;
        }
        if (!Expect(module.ModuleName() == "test-instance",
                    "ModuleName should return injected instance name")) {
            return 1;
        }

        if (!Expect(module.State() == ModuleState::Created,
                    "initial state should be Created")) {
            return 1;
        }

        foundation::base::Result<void> result = module.Start();
        if (!ExpectInvalidStateResult(
                result,
                "Start before Init should fail")) {
            return 1;
        }

        result = module.Stop();
        if (!ExpectInvalidStateResult(
                result,
                "Stop before Init should fail")) {
            return 1;
        }

        result = module.Init(ctx);
        if (!Expect(result.IsOk(), "Init should succeed from Created")) {
            return 1;
        }

        set_name_result = module.SetModuleName("renamed-instance");
        if (!Expect(
                !set_name_result.IsOk() &&
                    set_name_result.GetError() ==
                        foundation::base::ErrorCode::kInvalidState,
                "SetModuleName after Init should fail")) {
            return 1;
        }

        result = module.Init(ctx);
        if (!ExpectInvalidStateResult(
                result,
                "Init from Inited should fail")) {
            return 1;
        }

        result = module.Stop();
        if (!ExpectInvalidStateResult(
                result,
                "Stop from Inited should fail")) {
            return 1;
        }

        result = module.Fini();
        if (!Expect(result.IsOk(), "Fini should succeed from Inited")) {
            return 1;
        }

        if (!Expect(module.InitCalls() == 1, "OnInit should be called exactly once")) {
            return 1;
        }
        if (!Expect(module.StartCalls() == 0,
                    "OnStart should not be called in Inited -> Fini flow")) {
            return 1;
        }
        if (!Expect(module.StopCalls() == 0,
                    "OnStop should not be called in Inited -> Fini flow")) {
            return 1;
        }
        if (!Expect(module.FiniCalls() == 1, "OnFini should be called exactly once")) {
            return 1;
        }

        if (!Expect(module.State() == ModuleState::Fini,
                    "state should be Fini after Fini()")) {
            return 1;
        }
    }

    {
        TestModule module;
        foundation::base::Result<void> result = module.Init(ctx);
        if (!Expect(result.IsOk(), "Init should succeed before Started checks")) {
            return 1;
        }

        result = module.Start();
        if (!Expect(result.IsOk(), "Start should succeed from Inited")) {
            return 1;
        }

        result = module.Init(ctx);
        if (!ExpectInvalidStateResult(
                result,
                "Init from Started should fail")) {
            return 1;
        }

        result = module.Start();
        if (!ExpectInvalidStateResult(
                result,
                "Start from Started should fail")) {
            return 1;
        }

        result = module.Fini();
        if (!Expect(result.IsOk(), "Fini should succeed from Started")) {
            return 1;
        }

        if (!Expect(module.StopCalls() == 1,
                    "Started -> Fini should call OnStop exactly once")) {
            return 1;
        }
        if (!Expect(module.FiniCalls() == 1,
                    "Started -> Fini should call OnFini exactly once")) {
            return 1;
        }
        if (!Expect(module.State() == ModuleState::Fini,
                    "Started -> Fini should move state to Fini")) {
            return 1;
        }
    }

    {
        StopFailingModule module;
        foundation::base::Result<void> result = module.Init(ctx);
        if (!Expect(result.IsOk(), "Init should succeed before stop-failure fini")) {
            return 1;
        }

        result = module.Start();
        if (!Expect(result.IsOk(), "Start should succeed before stop-failure fini")) {
            return 1;
        }

        result = module.Fini();
        if (!Expect(!result.IsOk(), "Fini should surface OnStop failure")) {
            return 1;
        }
        if (!Expect(result.GetError() == foundation::base::ErrorCode::kInvalidState,
                    "Fini should return the OnStop error code")) {
            return 1;
        }
        if (!Expect(module.StopCalls() == 1,
                    "Fini should still invoke OnStop once when it fails")) {
            return 1;
        }
        if (!Expect(module.FiniCalls() == 1,
                    "Fini should continue to OnFini after OnStop failure")) {
            return 1;
        }
        if (!Expect(module.State() == ModuleState::Fini,
                    "Fini should still move to Fini after OnStop failure")) {
            return 1;
        }
    }

    {
        TestModule module;
        foundation::base::Result<void> result = module.Init(ctx);
        if (!Expect(result.IsOk(), "Init should succeed before Stopped checks")) {
            return 1;
        }

        result = module.Start();
        if (!Expect(result.IsOk(), "Start should succeed before Stopped checks")) {
            return 1;
        }

        result = module.Stop();
        if (!Expect(result.IsOk(), "Stop should succeed from Started")) {
            return 1;
        }

        result = module.Init(ctx);
        if (!ExpectInvalidStateResult(
                result,
                "Init from Stopped should fail")) {
            return 1;
        }

        result = module.Stop();
        if (!ExpectInvalidStateResult(
                result,
                "Stop from Stopped should fail")) {
            return 1;
        }

        result = module.Start();
        if (!Expect(result.IsOk(), "Start should succeed from Stopped")) {
            return 1;
        }
        if (!Expect(module.StartCalls() == 2,
                    "Stopped -> Start should increment OnStart call count")) {
            return 1;
        }

        result = module.Stop();
        if (!Expect(result.IsOk(), "Stop should succeed after restarting")) {
            return 1;
        }

        result = module.Fini();
        if (!Expect(result.IsOk(), "Fini should succeed from Stopped")) {
            return 1;
        }

        if (!Expect(module.StopCalls() == 2,
                    "OnStop should be called twice across stop-restart-stop flow")) {
            return 1;
        }
        if (!Expect(module.FiniCalls() == 1,
                    "OnFini should be called once from Stopped")) {
            return 1;
        }
        if (!Expect(module.State() == ModuleState::Fini,
                    "Stopped -> Fini should move state to Fini")) {
            return 1;
        }
    }

    {
        TestModule module;
        foundation::base::Result<void> result = module.SetModuleName("");
        if (!Expect(!result.IsOk(), "SetModuleName should reject empty names")) {
            return 1;
        }
        if (!Expect(result.GetError() == foundation::base::ErrorCode::kInvalidArgument,
                    "SetModuleName should return kInvalidArgument for empty names")) {
            return 1;
        }
    }

    {
        TestModule module;
        foundation::base::Result<void> result = module.Fini();
        if (!Expect(result.IsOk(), "Fini from Created should succeed")) {
            return 1;
        }
        if (!Expect(module.FiniCalls() == 0,
                    "Fini from Created should not call OnFini")) {
            return 1;
        }
        if (!Expect(module.State() == ModuleState::Fini,
                    "Fini from Created should move state to Fini")) {
            return 1;
        }

        result = module.Start();
        if (!ExpectInvalidStateResult(
                result,
                "Start from Fini should fail")) {
            return 1;
        }

        result = module.Stop();
        if (!ExpectInvalidStateResult(
                result,
                "Stop from Fini should fail")) {
            return 1;
        }

        result = module.Fini();
        if (!ExpectInvalidStateResult(
                result,
                "Fini from Fini should fail")) {
            return 1;
        }

        result = module.Init(ctx);
        if (!Expect(result.IsOk(), "Init should succeed from Fini")) {
            return 1;
        }
        if (!Expect(module.State() == ModuleState::Inited,
                    "Init from Fini should move state to Inited")) {
            return 1;
        }
    }

    std::cout << "[PASSED] module_base_lifecycle_test" << std::endl;
    return 0;
}
