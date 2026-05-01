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
    std::shared_ptr<RabbitMqBusConfig> config;
    std::map<std::string, MessageHandler> handlers;
    std::shared_ptr<foundation::concurrent::ThreadPool> worker_pool;
    std::shared_ptr<RabbitMqConnectionDriver> driver;
    bool stopping;
};

}  // namespace messaging
}  // namespace module_context
