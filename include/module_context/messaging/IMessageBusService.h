#pragma once

#include "module_context/framework/Export.h"
#include "module_context/messaging/Types.h"

#include "foundation/base/Result.h"

#include <string>

namespace module_context {
namespace messaging {

/**
 * @brief 消息总线服务公开接口。
 *
 * 外部使用者通过 `IContext::GetService<IMessageBusService>()` 获取该接口，
 * 用于发布消息、注册消费者处理器以及查询连接状态。
 */
class MC_FRAMEWORK_API IMessageBusService {
public:
    /**
     * @brief 析构服务接口。
     */
    virtual ~IMessageBusService() {}

    /**
     * @brief 发布消息。
     *
     * @param request 发布请求。
     * @return 成功返回 `Ok`；失败时返回连接或参数错误。
     */
    virtual foundation::base::Result<void> Publish(
        const PublishRequest& request) = 0;
    /**
     * @brief 异步发布消息。
     *
     * @param request 发布请求。
     * @return 当参数有效且消息已成功进入本地驱动命令队列时返回 `Ok`；
     *         若连接未就绪或参数非法，则返回对应错误。
     */
    virtual foundation::base::Result<void> PublishAsync(
        const PublishRequest& request) = 0;
    /**
     * @brief 注册指定消费者的消息处理回调。
     *
     * @param consumer_name 消费者逻辑名称。
     * @param handler 消息处理回调。
     * @return 成功返回 `Ok`；失败时返回参数错误、配置错误或状态错误。
     */
    virtual foundation::base::Result<void> RegisterConsumerHandler(
        const std::string& consumer_name,
        MessageHandler handler) = 0;
    /**
     * @brief 注销指定消费者的消息处理回调。
     *
     * @param consumer_name 消费者逻辑名称。
     * @return 成功返回 `Ok`；失败时返回参数错误。
     */
    virtual foundation::base::Result<void> UnregisterConsumerHandler(
        const std::string& consumer_name) = 0;
    /**
     * @brief 声明交换机。
     *
     * @param spec 交换机参数。
     * @return 成功返回 `Ok`；失败时返回连接或参数错误。
     */
    virtual foundation::base::Result<void> DeclareExchange(
        const ExchangeSpec& spec) = 0;
    /**
     * @brief 声明队列。
     *
     * @param spec 队列参数。
     * @return 成功返回 `Ok`；失败时返回连接或参数错误。
     */
    virtual foundation::base::Result<void> DeclareQueue(
        const QueueSpec& spec) = 0;
    /**
     * @brief 绑定交换机与队列。
     *
     * @param spec 绑定参数。
     * @return 成功返回 `Ok`；失败时返回连接或参数错误。
     */
    virtual foundation::base::Result<void> BindQueue(
        const BindingSpec& spec) = 0;
    /**
     * @brief 查询当前连接状态。
     *
     * @return 当前连接状态。
     */
    virtual ConnectionState GetConnectionState() const = 0;
};

}  // namespace messaging
}  // namespace module_context

namespace module_context {
namespace framework {

template <typename T>
struct ServiceTypeTraits;

/**
 * @brief `IMessageBusService` 的服务键映射。
 */
template <>
struct ServiceTypeTraits<module_context::messaging::IMessageBusService> {
    static const char* Key() {
        return "module_context.messaging.IMessageBusService";
    }
};

}  // namespace framework
}  // namespace module_context
