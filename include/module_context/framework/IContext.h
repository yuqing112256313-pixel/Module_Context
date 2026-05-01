#pragma once

#include "module_context/framework/Export.h"
#include "module_context/framework/IModule.h"

#include "foundation/base/ErrorCode.h"
#include "foundation/base/Result.h"

#include <string>

namespace module_context {
namespace framework {

class IModuleManager;

/**
 * @brief 服务接口类型到运行时服务键的映射。
 *
 * 公开服务接口需要在对应头文件中为该模板提供特化，供 `GetService()` 做类型安全查询。
 */
template <typename T>
struct ServiceTypeTraits;

/**
 * @brief 模块上下文公开接口。
 *
 * 上下文负责驱动整体生命周期，并作为对外统一的服务发现入口。
 */
class MC_FRAMEWORK_API IContext {
public:
    /**
     * @brief 获取进程内默认上下文单例。
     *
     * @return 默认上下文实例。
     */
    static IContext& Instance();

    /**
     * @brief 析构上下文接口。
     */
    virtual ~IContext() {}

    /**
     * @brief 初始化上下文内已装载的模块。
     *
     * @return 成功返回 `Ok`；失败时返回初始化错误。
     */
    virtual foundation::base::Result<void> Init() = 0;
    /**
     * @brief 启动上下文内已装载的模块。
     *
     * @return 成功返回 `Ok`；失败时返回启动错误。
     */
    virtual foundation::base::Result<void> Start() = 0;
    /**
     * @brief 停止上下文内已装载的模块。
     *
     * @return 成功返回 `Ok`；失败时返回停止错误。
     */
    virtual foundation::base::Result<void> Stop() = 0;
    /**
     * @brief 释放上下文内已装载的模块。
     *
     * @return 成功返回 `Ok`；失败时返回反初始化错误。
     */
    virtual foundation::base::Result<void> Fini() = 0;

    /**
     * @brief 获取模块管理器视图。
     *
     * @return 当前上下文关联的模块管理器；若实现不可用可返回空指针。
     */
    virtual IModuleManager* ModuleManager() = 0;

    /**
     * @brief 按服务接口类型和模块实例名查询服务。
     *
     * @tparam T 服务接口类型，需提供 `ServiceTypeTraits<T>` 特化。
     * @param name 提供该服务的模块实例名。
     * @return 成功时返回服务接口指针；未找到或类型不匹配时返回错误。
     */
    template <typename T>
    foundation::base::Result<T*> GetService(const std::string& name) {
        foundation::base::Result<IModule*> service =
            LookupServiceRaw(ServiceTypeTraits<T>::Key(), name);
        if (!service.IsOk()) {
            return foundation::base::Result<T*>(
                service.GetError(),
                service.GetMessage());
        }

        T* typed_service = dynamic_cast<T*>(service.Value());
        if (typed_service == NULL) {
            return foundation::base::Result<T*>(
                foundation::base::ErrorCode::kInvalidState,
                "Service type cast failed");
        }

        return foundation::base::Result<T*>(typed_service);
    }

    /**
     * @brief 查询当前上下文中唯一的指定类型服务。
     *
     * @tparam T 服务接口类型，需提供 `ServiceTypeTraits<T>` 特化。
     * @return 成功时返回服务接口指针；当不存在或存在多个实例时返回错误。
     *
     * @note 该重载只适用于上下文中该类型服务恰好注册了一个实例的场景。
     */
    template <typename T>
    foundation::base::Result<T*> GetService() {
        foundation::base::Result<IModule*> service =
            LookupUniqueServiceRaw(ServiceTypeTraits<T>::Key());
        if (!service.IsOk()) {
            return foundation::base::Result<T*>(
                service.GetError(),
                service.GetMessage());
        }

        T* typed_service = dynamic_cast<T*>(service.Value());
        if (typed_service == NULL) {
            return foundation::base::Result<T*>(
                foundation::base::ErrorCode::kInvalidState,
                "Service type cast failed");
        }

        return foundation::base::Result<T*>(typed_service);
    }

private:
    /**
     * @brief 供模板查询封装使用的具名服务查找钩子。
     *
     * @param service_key 服务接口键。
     * @param name 提供该服务的模块实例名。
     * @return 成功时返回原始模块接口；失败时返回错误。
     */
    virtual foundation::base::Result<IModule*> LookupServiceRaw(
        const char* service_key,
        const std::string& name) = 0;
    /**
     * @brief 供模板查询封装使用的唯一服务查找钩子。
     *
     * @param service_key 服务接口键。
     * @return 成功时返回原始模块接口；失败时返回错误。
     */
    virtual foundation::base::Result<IModule*> LookupUniqueServiceRaw(
        const char* service_key) = 0;
};

}  // namespace framework
}  // namespace module_context
