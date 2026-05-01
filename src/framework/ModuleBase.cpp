#include "framework/ModuleBase.h"

#include "foundation/base/Assert.h"
#include "foundation/base/ErrorCode.h"
#include "foundation/base/Result.h"

namespace module_context {
namespace framework {

namespace {

// 状态表顺序必须与 ModuleState 枚举值完全一致，且枚举值必须从 0 连续递增。
const int kModuleStateCount = static_cast<int>(ModuleState::Fini) + 1;

const bool kValidTransitions[kModuleStateCount][kModuleStateCount] = {
    // 列表示目标状态，行表示当前状态。
    /* 当前为 Created */ {false, true,   false,   false,   true},
    /* 当前为 Inited  */ {false, false,  true,    false,   true},
    /* 当前为 Started */ {false, false,  false,   true,    true},
    /* 当前为 Stopped */ {false, false,  true,    false,   true},
    /* 当前为 Fini    */ {false, true,   false,   false,   false}
};

foundation::base::Result<void> MakeStateError(
    const char* operation,
    const std::string& module_name,
    ModuleState state) {
    return foundation::base::Result<void>(
        foundation::base::ErrorCode::kInvalidState,
        std::string("ModuleBase::") + operation + " failed for module '" +
            module_name + "' from state " +
            std::to_string(static_cast<int>(state)));
}

foundation::base::Result<void> PrefixHookError(
    const char* operation,
    const std::string& module_name,
    const foundation::base::Result<void>& result) {
    if (result.IsOk()) {
        return result;
    }

    return foundation::base::Result<void>(
        result.GetError(),
        std::string("ModuleBase::") + operation + " failed for module '" +
            module_name + "': " + result.GetMessage());
}

}  // namespace

ModuleBase::ModuleBase()
    : ctx_(NULL),
      module_name_(),
      state_(ModuleState::Created) {
}

ModuleBase::~ModuleBase() {
}

std::string ModuleBase::ModuleName() const {
    if (!module_name_.empty()) {
        return module_name_;
    }

    return ModuleType();
}

foundation::base::Result<void> ModuleBase::SetModuleName(
    const std::string& name) {
    if (name.empty()) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidArgument,
            "ModuleBase::SetModuleName failed: name is empty");
    }

    if (state_ != ModuleState::Created) {
        return foundation::base::Result<void>(
            foundation::base::ErrorCode::kInvalidState,
            "ModuleBase::SetModuleName failed: module is not in Created state");
    }

    module_name_ = name;
    return foundation::base::MakeSuccess();
}

ModuleState ModuleBase::State() const {
    return state_;
}

IContext& ModuleBase::Context() const {
    FOUNDATION_ASSERT_MSG(
        ctx_ != NULL,
        "ModuleBase::Context() called before module initialization");
    return *ctx_;
}

bool ModuleBase::HasContext() const {
    return ctx_ != NULL;
}

foundation::base::Result<void> ModuleBase::OnInit() {
    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> ModuleBase::OnStart() {
    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> ModuleBase::OnStop() {
    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> ModuleBase::OnFini() {
    return foundation::base::MakeSuccess();
}

bool ModuleBase::IsValidTransition(ModuleState from, ModuleState to) {
    const int from_index = static_cast<int>(from);
    const int to_index = static_cast<int>(to);

    if (from_index < 0 || from_index >= kModuleStateCount ||
        to_index < 0 || to_index >= kModuleStateCount) {
        return false;
    }

    return kValidTransitions[from_index][to_index];
}

foundation::base::Result<void> ModuleBase::Init(IContext& ctx) {
    if (!IsValidTransition(state_, ModuleState::Inited)) {
        return MakeStateError("Init", ModuleName(), state_);
    }

    ctx_ = &ctx;
    foundation::base::Result<void> init_result = OnInit();
    if (!init_result.IsOk()) {
        // 初始化失败时撤回上下文注入，避免半初始化模块继续访问外部服务。
        ctx_ = NULL;
        return PrefixHookError("Init", ModuleName(), init_result);
    }

    state_ = ModuleState::Inited;
    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> ModuleBase::Start() {
    if (!IsValidTransition(state_, ModuleState::Started)) {
        return MakeStateError("Start", ModuleName(), state_);
    }

    foundation::base::Result<void> start_result = OnStart();
    if (!start_result.IsOk()) {
        return PrefixHookError("Start", ModuleName(), start_result);
    }

    state_ = ModuleState::Started;
    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> ModuleBase::Stop() {
    if (!IsValidTransition(state_, ModuleState::Stopped)) {
        return MakeStateError("Stop", ModuleName(), state_);
    }

    foundation::base::Result<void> stop_result = OnStop();
    if (!stop_result.IsOk()) {
        return PrefixHookError("Stop", ModuleName(), stop_result);
    }

    state_ = ModuleState::Stopped;
    return foundation::base::MakeSuccess();
}

foundation::base::Result<void> ModuleBase::Fini() {
    if (!IsValidTransition(state_, ModuleState::Fini)) {
        return MakeStateError("Fini", ModuleName(), state_);
    }

    foundation::base::Result<void> first_error =
        foundation::base::MakeSuccess();
    ModuleState cleanup_state = state_;

    if (IsValidTransition(cleanup_state, ModuleState::Stopped)) {
        foundation::base::Result<void> stop_result = OnStop();
        if (!stop_result.IsOk()) {
            first_error = PrefixHookError("Fini", ModuleName(), stop_result);
        }

        // 即使 Stop 钩子报错，也继续执行 Fini 钩子，尽量释放初始化阶段资源。
        cleanup_state = ModuleState::Stopped;
    }

    if (cleanup_state != ModuleState::Created &&
        IsValidTransition(cleanup_state, ModuleState::Fini)) {
        foundation::base::Result<void> fini_result = OnFini();
        if (first_error.IsOk() && !fini_result.IsOk()) {
            first_error = PrefixHookError("Fini", ModuleName(), fini_result);
        }
    }

    ctx_ = NULL;
    state_ = ModuleState::Fini;
    return first_error;
}

}  // namespace framework
}  // namespace module_context
