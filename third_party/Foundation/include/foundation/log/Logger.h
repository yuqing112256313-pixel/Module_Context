#ifndef FOUNDATION_LOG_LOGGER_H_
#define FOUNDATION_LOG_LOGGER_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "foundation/base/Compiler.h"
#include "foundation/base/Export.h"
#include "foundation/base/NonCopyable.h"

namespace foundation {
namespace log {

enum class LogLevel {
    kTrace = 0,
    kDebug = 1,
    kInfo = 2,
    kWarning = 3,
    kError = 4,
    kFatal = 5,
    kOff = 6
};

FOUNDATION_API const char* LogLevelToString(LogLevel level);

class FOUNDATION_API ILogSink {
public:
    virtual ~ILogSink() {}

    virtual void Write(LogLevel level,
                       const char* timestamp,
                       const char* file,
                       int line,
                       const char* func,
                       const char* message) = 0;
};

class FOUNDATION_API ConsoleSink : public ILogSink,
                                   private foundation::base::NonCopyable {
public:
    ConsoleSink();
    virtual ~ConsoleSink();

    virtual void Write(LogLevel level,
                       const char* timestamp,
                       const char* file,
                       int line,
                       const char* func,
                       const char* message);

private:
    std::mutex mutex_;
};

class FOUNDATION_API FileSink : public ILogSink,
                                private foundation::base::NonCopyable {
public:
    explicit FileSink(const std::string& path, bool append = true);
    virtual ~FileSink();

    virtual void Write(LogLevel level,
                       const char* timestamp,
                       const char* file,
                       int line,
                       const char* func,
                       const char* message);

    bool IsOpen() const;

private:
    mutable std::mutex mutex_;
    void* file_;  // std::ofstream*
};

class FOUNDATION_API Logger : private foundation::base::NonCopyable {
public:
    static Logger& Instance();

    void SetLevel(LogLevel level);
    LogLevel GetLevel() const;
    bool IsLevelEnabled(LogLevel level) const;

    void AddSink(const std::shared_ptr<ILogSink>& sink);
    void ClearSinks();

    void Log(LogLevel level,
             const char* file,
             int line,
             const char* func,
             const std::string& message);

private:
    Logger();
    ~Logger();

    std::string GetTimestamp() const;

private:
    std::atomic<int> level_;
    mutable std::mutex sinks_mutex_;
    std::vector<std::shared_ptr<ILogSink> > sinks_;
};

}  // namespace log
}  // namespace foundation

#define FOUNDATION_LOG_IMPL_(level, message_expr)                             \
    do {                                                                      \
        ::foundation::log::Logger& _foundation_logger_ =                      \
            ::foundation::log::Logger::Instance();                            \
        if (_foundation_logger_.IsLevelEnabled(level)) {                      \
            std::ostringstream _foundation_log_stream_;                       \
            _foundation_log_stream_ << message_expr;                          \
            _foundation_logger_.Log(                                          \
                level, __FILE__, __LINE__, FOUNDATION_FUNCTION,               \
                _foundation_log_stream_.str());                               \
        }                                                                     \
    } while (0)

#define FOUNDATION_LOG_TRACE(message_expr)                                    \
    FOUNDATION_LOG_IMPL_(::foundation::log::LogLevel::kTrace, message_expr)
#define FOUNDATION_LOG_DEBUG(message_expr)                                    \
    FOUNDATION_LOG_IMPL_(::foundation::log::LogLevel::kDebug, message_expr)
#define FOUNDATION_LOG_INFO(message_expr)                                     \
    FOUNDATION_LOG_IMPL_(::foundation::log::LogLevel::kInfo, message_expr)
#define FOUNDATION_LOG_WARNING(message_expr)                                  \
    FOUNDATION_LOG_IMPL_(::foundation::log::LogLevel::kWarning, message_expr)
#define FOUNDATION_LOG_ERROR(message_expr)                                    \
    FOUNDATION_LOG_IMPL_(::foundation::log::LogLevel::kError, message_expr)
#define FOUNDATION_LOG_FATAL(message_expr)                                    \
    FOUNDATION_LOG_IMPL_(::foundation::log::LogLevel::kFatal, message_expr)

#define FOUNDATION_LOG_RESULT_INFO(prefix, result_expr)                       \
    do {                                                                      \
        auto _foundation_result_ = (result_expr);                             \
        if (_foundation_result_.IsOk()) {                                     \
            FOUNDATION_LOG_INFO(prefix << _foundation_result_.Value());       \
        } else {                                                              \
            FOUNDATION_LOG_ERROR(#result_expr << " failed, code="             \
                << ::foundation::base::ErrorCodeToString(                     \
                       _foundation_result_.GetError())                        \
                << ", message=" << _foundation_result_.GetMessage());         \
        }                                                                     \
    } while (0)

#endif  // FOUNDATION_LOG_LOGGER_H_
