#ifndef FOUNDATION_PLUGIN_PLUGINLOADER_H_
#define FOUNDATION_PLUGIN_PLUGINLOADER_H_

#include <memory>
#include <string>
#include <utility>

#include "foundation/base/ErrorCode.h"
#include "foundation/base/Export.h"
#include "foundation/base/NonCopyable.h"
#include "foundation/base/Result.h"
#include "foundation/plugin/DynamicLibrary.h"

namespace foundation {
namespace plugin {

// ============================================================================
// PluginLoader<Base>
//
// Standard plugin loader built on top of DynamicLibrary.
//
// Expected exported C symbols from plugin:
//
//   extern "C" int GetPluginApiVersion();
//   extern "C" Base* CreatePlugin();
//   extern "C" void DestroyPlugin(Base* instance);
//
// Lifecycle guarantee:
//   PluginHandle holds a shared library state. As long as any PluginHandle
//   remains alive, the underlying shared library stays loaded. This prevents
//   destroy_fn from becoming a dangling function pointer after PluginLoader::Close().
//
// Notes:
//   1. Base should have a virtual destructor.
//   2. PluginLoader owns one shared reference to the loaded library state.
//   3. Each PluginHandle owns another shared reference to that state.
//   4. PluginLoader::Close() releases only the loader's reference.
//   5. Actual DLL/SO unload happens only after the last PluginHandle is gone.
// ============================================================================
template <typename Base>
class PluginLoader : private foundation::base::NonCopyable {
public:
    typedef int (*GetPluginApiVersionFn)();
    typedef Base* (*CreatePluginFn)();
    typedef void (*DestroyPluginFn)(Base*);

private:
    struct SharedState {
        SharedState()
            : library(),
              loaded_api_version(0),
              create_fn(NULL),
              destroy_fn(NULL) {
        }

        DynamicLibrary library;
        int loaded_api_version;
        CreatePluginFn create_fn;
        DestroyPluginFn destroy_fn;
    };

public:
    class PluginHandle : private foundation::base::NonCopyable {
    public:
        PluginHandle()
            : instance_(NULL),
              state_() {
        }

        ~PluginHandle() {
            Reset();
        }

        PluginHandle(PluginHandle&& other)
            : instance_(other.instance_),
              state_(std::move(other.state_)) {
            other.instance_ = NULL;
        }

        PluginHandle& operator=(PluginHandle&& other) {
            if (this != &other) {
                Reset();
                instance_ = other.instance_;
                state_ = std::move(other.state_);
                other.instance_ = NULL;
            }
            return *this;
        }

        Base* Get() const {
            return instance_;
        }

        Base& operator*() const {
            return *instance_;
        }

        Base* operator->() const {
            return instance_;
        }

        bool IsValid() const {
            return instance_ != NULL;
        }

        void Reset() {
            if (instance_ != NULL &&
                state_ &&
                state_->destroy_fn != NULL) {
                state_->destroy_fn(instance_);
            }
            instance_ = NULL;
            state_.reset();
        }

    private:
        friend class PluginLoader<Base>;

        PluginHandle(Base* instance, const std::shared_ptr<SharedState>& state)
            : instance_(instance),
              state_(state) {
        }

    private:
        Base* instance_;
        std::shared_ptr<SharedState> state_;
    };

public:
    PluginLoader()
        : state_() {
    }

    ~PluginLoader() {
        (void)Close();
    }

public:
    foundation::base::Result<void> Open(const std::string& path,
                                        int expected_api_version) {
        if (path.empty()) {
            return foundation::base::Result<void>(
                foundation::base::ErrorCode::kInvalidArgument,
                "PluginLoader::Open failed: path is empty");
        }

        if (expected_api_version <= 0) {
            return foundation::base::Result<void>(
                foundation::base::ErrorCode::kInvalidArgument,
                "PluginLoader::Open failed: expected_api_version must be > 0");
        }

        if (state_) {
            return foundation::base::Result<void>(
                foundation::base::ErrorCode::kInvalidState,
                "PluginLoader::Open failed: plugin already open");
        }

        std::shared_ptr<SharedState> new_state(new SharedState());

        foundation::base::Result<void> open_result =
            new_state->library.Open(path);
        if (!open_result.IsOk()) {
            return open_result;
        }

        foundation::base::Result<GetPluginApiVersionFn> version_fn =
            new_state->library.template GetSymbol<GetPluginApiVersionFn>(
                "GetPluginApiVersion");
        if (!version_fn.IsOk()) {
            (void)new_state->library.Close();
            return foundation::base::Result<void>(
                version_fn.GetError(),
                std::string("PluginLoader::Open failed: missing GetPluginApiVersion: ") +
                    version_fn.GetMessage());
        }

        foundation::base::Result<CreatePluginFn> create_fn =
            new_state->library.template GetSymbol<CreatePluginFn>("CreatePlugin");
        if (!create_fn.IsOk()) {
            (void)new_state->library.Close();
            return foundation::base::Result<void>(
                create_fn.GetError(),
                std::string("PluginLoader::Open failed: missing CreatePlugin: ") +
                    create_fn.GetMessage());
        }

        foundation::base::Result<DestroyPluginFn> destroy_fn =
            new_state->library.template GetSymbol<DestroyPluginFn>("DestroyPlugin");
        if (!destroy_fn.IsOk()) {
            (void)new_state->library.Close();
            return foundation::base::Result<void>(
                destroy_fn.GetError(),
                std::string("PluginLoader::Open failed: missing DestroyPlugin: ") +
                    destroy_fn.GetMessage());
        }

        const int actual_api_version = version_fn.Value()();
        if (actual_api_version != expected_api_version) {
            (void)new_state->library.Close();
            return foundation::base::Result<void>(
                foundation::base::ErrorCode::kVersionMismatch,
                "PluginLoader::Open failed: plugin API version mismatch");
        }

        new_state->loaded_api_version = actual_api_version;
        new_state->create_fn = create_fn.Value();
        new_state->destroy_fn = destroy_fn.Value();
        state_ = new_state;

        return foundation::base::Result<void>();
    }

    foundation::base::Result<void> Close() {
        // Release only loader's ownership. If any PluginHandle still holds
        // the shared state, the library remains loaded until the last handle dies.
        state_.reset();
        return foundation::base::Result<void>();
    }

    bool IsOpen() const {
        return state_ && state_->library.IsOpen();
    }

    const std::string& Path() const {
        static const std::string kEmptyPath;
        if (!state_) {
            return kEmptyPath;
        }
        return state_->library.Path();
    }

    int LoadedApiVersion() const {
        if (!state_) {
            return 0;
        }
        return state_->loaded_api_version;
    }

    foundation::base::Result<PluginHandle> Create() const {
        if (!state_) {
            return foundation::base::Result<PluginHandle>(
                foundation::base::ErrorCode::kInvalidState,
                "PluginLoader::Create failed: plugin not open");
        }

        if (!state_->library.IsOpen()) {
            return foundation::base::Result<PluginHandle>(
                foundation::base::ErrorCode::kInvalidState,
                "PluginLoader::Create failed: plugin library is not open");
        }

        if (state_->create_fn == NULL || state_->destroy_fn == NULL) {
            return foundation::base::Result<PluginHandle>(
                foundation::base::ErrorCode::kInvalidState,
                "PluginLoader::Create failed: plugin entry points unavailable");
        }

        Base* instance = state_->create_fn();
        if (instance == NULL) {
            return foundation::base::Result<PluginHandle>(
                foundation::base::ErrorCode::kOperationFailed,
                "PluginLoader::Create failed: CreatePlugin returned null");
        }

        return foundation::base::Result<PluginHandle>(
            PluginHandle(instance, state_));
    }

private:
    std::shared_ptr<SharedState> state_;
};

}  // namespace plugin
}  // namespace foundation

#endif  // FOUNDATION_PLUGIN_PLUGINLOADER_H_
