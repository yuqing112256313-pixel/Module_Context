#pragma once

#include "module_context/framework/Export.h"
#include "module_context/messaging/Types.h"

#include "foundation/base/Result.h"

#include <string>

namespace module_context {
namespace messaging {

/**
 * @brief AMQP 风格消息总线服务公开接口。
 *
 * 外部使用者通过 `IContext::GetService<IMessageBusService>()` 获取该接口，
 * 用于发布消息、注册消费者处理器、动态声明拓扑以及查询连接状态。
 *
 * 该接口抽象的是 AMQP 0-9-1 常见语义：exchange、queue、routing key、
 * consumer 和 header table。它不暴露具体 broker 的管理 API，也不要求调用方
 * 感知模块内部使用的连接驱动。若未来需要接入非 AMQP 消息系统，应优先新增
 * 独立能力接口或适配层，而不是把非 AMQP 语义硬塞进本接口。
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
     * 同步发布会等待消息命令进入底层 AMQP channel。它不等价于 broker 持久化确认；
     * 需要 publisher confirm 时应另行扩展确认语义。
     *
     * @param request 发布请求。
     * @return 成功返回 `Ok`；失败时返回连接或参数错误。
     */
    virtual foundation::base::Result<void> Publish(
        const PublishRequest& request) = 0;
    /**
     * @brief 异步发布消息。
     *
     * 异步发布只保证本地驱动接受了发布命令。连接在命令实际执行前仍可能断开，
     * 因此调用方不应把 `Ok` 理解成消息已被 broker 接收。
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
     * handler 在模块内部 worker 线程中执行。返回 `Ack`、`Requeue` 或 `Reject`
     * 用于决定非自动确认消息的最终处置。
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
     * 该接口用于运行期补充或更新 AMQP 拓扑；模块启动阶段也会按配置声明一次拓扑。
     *
     * @param spec 交换机参数。
     * @return 成功返回 `Ok`；失败时返回连接或参数错误。
     */
    virtual foundation::base::Result<void> DeclareExchange(
        const ExchangeSpec& spec) = 0;
    /**
     * @brief 声明队列。
     *
     * 队列声明参数遵循 AMQP 语义；具体 broker 是否支持某些 arguments 由 broker
     * 自身决定。
     *
     * @param spec 队列参数。
     * @return 成功返回 `Ok`；失败时返回连接或参数错误。
     */
    virtual foundation::base::Result<void> DeclareQueue(
        const QueueSpec& spec) = 0;
    /**
     * @brief 绑定交换机与队列。
     *
     * 默认交换机的绑定由 AMQP broker 隐式维护，不应通过该接口声明。
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
