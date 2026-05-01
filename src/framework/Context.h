#pragma once

#include "module_context/framework/IContext.h"

#include "foundation/base/NonCopyable.h"
#include "foundation/base/Result.h"

#include <memory>

namespace module_context {
namespace framework {

class ModuleManager;

/**
 * @brief 框架默认上下文实现。
 *
 * `Context` 只保存一个模块管理器，并把生命周期调用和服务查询委托给它。
 * 这样上层应用只需要面对 `IContext`，而具体的装载、顺序和服务注册细节集中在
 * `ModuleManager` 内维护。
 */
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
    // 上下文拥有模块管理器，因此模块和插件句柄的生命周期最终由上下文收束。
    std::unique_ptr<module_context::framework::ModuleManager> module_manager_;
};

}  // namespace framework
}  // namespace module_context
