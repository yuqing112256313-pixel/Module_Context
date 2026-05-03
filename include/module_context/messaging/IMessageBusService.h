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
     * 同步发布会等待消息命令进入底层 AMQP channel。它不等价于 broker 确认；
     * 需要可靠发布时应使用 `PublishConfirmed()`。
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
     * @brief 发布消息并等待 broker publisher confirm。
     *
     * 该接口用于需要可靠发布语义的调用方。返回 `Ok` 表示已经得到 broker 的明确
     * 处置回执；具体是否接收、拒绝或 mandatory 不可路由由 `PublishReceipt`
     * 表达。超时、连接中断或实现不支持确认能力会返回错误。
     *
     * `options.require_routable` 会为本次发布启用 AMQP mandatory 语义，用于确认
     * 消息至少路由到一个队列。它和 publisher confirm 是两层不同保证：前者关注
     * 路由，后者关注 broker 是否接收发布命令。
     *
     * @param request 发布请求。
     * @param options 确认等待选项。
     * @return 成功返回 broker 处置回执；失败返回连接、超时或能力错误。
     */
    virtual foundation::base::Result<PublishReceipt> PublishConfirmed(
        const PublishRequest& request,
        const PublishConfirmOptions& options) = 0;
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
     * @brief 启动已配置消费者。
     *
     * consumer_name 对应 `ConsumerSpec::name`，不是队列名。该接口用于运行期打开
     * 某个消费者的投递流；若消费者已经处于启动状态，调用应保持幂等并返回成功。
     *
     * @param consumer_name 消费者逻辑名称。
     * @return 成功返回 `Ok`；失败时返回参数、状态、连接或 broker 错误。
     */
    virtual foundation::base::Result<void> StartConsumer(
        const std::string& consumer_name) = 0;
    /**
     * @brief 停止已配置消费者。
     *
     * 停止消费者只取消该 consumer tag 的新投递，不删除队列、绑定或消息处理回调。
     * 若消费者已经停止，调用应保持幂等并返回成功。
     *
     * @param consumer_name 消费者逻辑名称。
     * @return 成功返回 `Ok`；失败时返回参数、状态、连接或 broker 错误。
     */
    virtual foundation::base::Result<void> StopConsumer(
        const std::string& consumer_name) = 0;
    /**
     * @brief 注册连接状态变化回调。
     *
     * handler 用于观察 AMQP 连接驱动的状态转移，例如连接成功、断线重连或停机。
     * 注册后只接收后续状态变化；若需要当前状态，应同时调用 `GetConnectionState()`。
     *
     * @param handler_name 观察者名称，需在当前服务内唯一。
     * @param handler 状态变化回调。
     * @return 成功返回 `Ok`；失败时返回参数错误或状态错误。
     */
    virtual foundation::base::Result<void> RegisterConnectionStateHandler(
        const std::string& handler_name,
        ConnectionStateHandler handler) = 0;
    /**
     * @brief 注销连接状态变化回调。
     *
     * @param handler_name 注册时使用的观察者名称。
     * @return 成功返回 `Ok`；失败时返回参数错误或未找到。
     */
    virtual foundation::base::Result<void> UnregisterConnectionStateHandler(
        const std::string& handler_name) = 0;
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
    /**
     * @brief 获取最近一次消息总线错误诊断。
     *
     * 该值是诊断快照，不替代每次调用返回的 `Result`。并发调用时，调用方应优先
     * 依据本次操作的 `Result` 或 `PublishReceipt` 判断结果，再把该快照用于日志
     * 与排障。
     *
     * @return 最近一次连接、协议或可靠发布错误信息。
     */
    virtual MessageBusErrorInfo GetLastErrorInfo() const = 0;
    /**
     * @brief 查询当前服务实现是否支持指定 AMQP 能力。
     *
     * @param feature 待查询能力。
     * @return 支持返回 `true`，否则返回 `false`。
     */
    virtual bool SupportsFeature(MessageBusFeature feature) const = 0;
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
