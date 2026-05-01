#pragma once

#include "module_context/framework/IModule.h"

#include "foundation/base/NonCopyable.h"
#include "foundation/base/Result.h"

#include <string>
#include <unordered_map>

namespace module_context {
namespace framework {

/**
 * @brief 运行时服务注册表。
 *
 * 服务注册表只保存“能力键 -> 模块实例名 -> 模块指针”的映射，不拥有模块对象。
 * 这种设计让模块可以通过继承多个服务接口暴露多种能力，同时仍由 `ModuleManager`
 * 统一管理实例生命周期。
 */
class ServiceRegistry : private foundation::base::NonCopyable {
public:
    ServiceRegistry();
    ~ServiceRegistry();

    /**
     * @brief 登记一个具名服务提供者。
     *
     * @param service_key 服务接口键。
     * @param name 模块实例名。
     * @param provider 提供该服务的模块实例，不转移所有权。
     */
    void Register(
        const std::string& service_key,
        const std::string& name,
        IModule* provider);
    /**
     * @brief 根据模块实际实现的已知服务接口自动登记能力。
     *
     * @param name 模块实例名。
     * @param provider 模块实例，不转移所有权。
     */
    void RegisterKnownServices(
        const std::string& name,
        IModule* provider);
    /**
     * @brief 查询某个能力键下的指定实例。
     */
    foundation::base::Result<IModule*> Lookup(
        const std::string& service_key,
        const std::string& name) const;
    /**
     * @brief 查询某个能力键下唯一的服务实例。
     *
     * 若该能力没有实例或存在多个实例，均返回错误，避免调用方无意中绑定到不确定对象。
     */
    foundation::base::Result<IModule*> LookupUnique(
        const std::string& service_key) const;
    /**
     * @brief 清空所有服务映射。
     *
     * 模块反初始化和插件句柄释放前应先清空注册表，避免留下悬空指针。
     */
    void Clear();

private:
    typedef std::unordered_map<std::string, IModule*> ServiceMap;
    typedef std::unordered_map<std::string, ServiceMap> ServicesByKey;

    ServicesByKey services_by_key_;
};

}  // namespace framework
}  // namespace module_context
