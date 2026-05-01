#pragma once

#include "foundation/base/NonCopyable.h"
#include "foundation/base/Result.h"

#include <map>
#include <string>

namespace Hh {
namespace Api {
namespace Plugin {
class IPlugin;
}  // namespace Plugin
}  // namespace Api
}  // namespace Hh

namespace module_context {
namespace plugin {

/**
 * @brief 已加载工序插件的实例注册表。
 *
 * 以插件实例名为键，维护 `name → IPlugin*` 映射。
 * 生命周期管理（创建/销毁）由 `PluginLoader` 负责，本类仅持有原始指针引用。
 */
class PluginRegistry : private foundation::base::NonCopyable {
public:
    PluginRegistry();
    ~PluginRegistry();

    /**
     * @brief 注册插件实例。
     *
     * @param name     插件实例名；为空或 instance 为 NULL 时静默忽略。
     * @param instance 插件实例指针，由 PluginLoader::Load 创建。
     */
    void Register(
        const std::string& name,
        Hh::Api::Plugin::IPlugin* instance);

    /**
     * @brief 按实例名查找插件指针。
     *
     * @param name 插件实例名。
     * @return 成功时返回 IPlugin 指针；未找到时返回 kNotFound 错误。
     */
    foundation::base::Result<Hh::Api::Plugin::IPlugin*> Lookup(
        const std::string& name) const;

    /**
     * @brief 判断指定名称的插件是否已注册。
     */
    bool Contains(const std::string& name) const;

    /**
     * @brief 清空所有注册记录（不释放插件资源）。
     */
    void Clear();

private:
    typedef std::map<std::string, Hh::Api::Plugin::IPlugin*> PluginMap;

    PluginMap plugins_;
};

}  // namespace plugin
}  // namespace module_context
