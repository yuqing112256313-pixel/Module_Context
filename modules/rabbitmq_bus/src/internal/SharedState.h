#pragma once

#include "ConfigTypes.h"
#include "module_context/messaging/Types.h"

#include "foundation/concurrent/ThreadPool.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace module_context {
namespace messaging {

class RabbitMqConnectionDriver;

/**
 * @brief 模块、服务 proxy、连接驱动回调之间共享的运行态容器。
 *
 * `MessageBusServiceProxy` 不拥有 driver，只在每次调用时从这里取当前 driver。
 * 模块 Stop/Start 可以替换 driver 或 worker pool，而已经暴露出去的服务指针
 * 仍然保持稳定。
 */
struct RabbitMqBusSharedState {
    RabbitMqBusSharedState()
        : mutex(),
          config(new RabbitMqBusConfig()),
          handlers(),
          worker_pool(),
          driver(),
          stopping(false) {
    }

    std::mutex mutex;
    // 最近一次 Init 解析出的配置。Start 时按该配置创建 driver 和 worker pool。
    std::shared_ptr<RabbitMqBusConfig> config;
    // 业务注册的消费者处理器，key 为 ConsumerSpec::name。
    std::map<std::string, MessageHandler> handlers;
    // 消费回调的业务执行线程池；AMQP 驱动线程只负责网络和协议推进。
    std::shared_ptr<foundation::concurrent::ThreadPool> worker_pool;
    // 当前连接驱动。Stop/Start 会替换或停止该对象。
    std::shared_ptr<RabbitMqConnectionDriver> driver;
    // Stop 过程中置位，让新到达的投递快速 requeue，避免继续提交业务任务。
    bool stopping;
};

}  // namespace messaging
}  // namespace module_context
