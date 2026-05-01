#include "foundation/log/Logger.h"

#include <fstream>
#include <iostream>

#include "foundation/time/TimeUtils.h"

namespace foundation {
namespace log {

namespace {

std::ofstream* ToOfstream(void* file_ptr) {
    return static_cast<std::ofstream*>(file_ptr);
}

}  // namespace

const char* LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::kTrace:
            return "TRACE";
        case LogLevel::kDebug:
            return "DEBUG";
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarning:
            return "WARN";
        case LogLevel::kError:
            return "ERROR";
        case LogLevel::kFatal:
            return "FATAL";
        case LogLevel::kOff:
            return "OFF";
        default:
            return "UNKNOWN";
    }
}

ConsoleSink::ConsoleSink() {
}

ConsoleSink::~ConsoleSink() {
}

void ConsoleSink::Write(LogLevel level,
                        const char* timestamp,
                        const char* file,
                        int line,
                        const char* func,
                        const char* message) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostream& os =
        (level == LogLevel::kWarning ||
         level == LogLevel::kError ||
         level == LogLevel::kFatal)
            ? std::cerr
            : std::cout;

    os << "[" << (timestamp != NULL ? timestamp : "UNKNOWN_TIME") << "]"
       << "[" << LogLevelToString(level) << "] "
       << (message != NULL ? message : "");

    if (file != NULL && file[0] != '\0') {
        os << " (" << file << ":" << line;
        if (func != NULL && func[0] != '\0') {
            os << ", " << func;
        }
        os << ")";
    }

    os << std::endl;
}

FileSink::FileSink(const std::string& path, bool append)
    : mutex_(),
      file_(NULL) {
    std::ios::openmode mode = std::ios::out;
    if (append) {
        mode |= std::ios::app;
    }

    std::ofstream* stream = new std::ofstream(path.c_str(), mode);
    if (stream->is_open()) {
        file_ = stream;
    } else {
        delete stream;
    }
}

FileSink::~FileSink() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ofstream* stream = ToOfstream(file_);
    if (stream != NULL) {
        if (stream->is_open()) {
            stream->close();
        }
        delete stream;
        file_ = NULL;
    }
}

void FileSink::Write(LogLevel level,
                     const char* timestamp,
                     const char* file,
                     int line,
                     const char* func,
                     const char* message) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ofstream* stream = ToOfstream(file_);
    if (stream == NULL || !stream->is_open()) {
        return;
    }

    (*stream) << "[" << (timestamp != NULL ? timestamp : "UNKNOWN_TIME") << "]"
              << "[" << LogLevelToString(level) << "] "
              << (message != NULL ? message : "");

    if (file != NULL && file[0] != '\0') {
        (*stream) << " (" << file << ":" << line;
        if (func != NULL && func[0] != '\0') {
            (*stream) << ", " << func;
        }
        (*stream) << ")";
    }

    (*stream) << std::endl;
}

bool FileSink::IsOpen() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ofstream* stream = ToOfstream(file_);
    return stream != NULL && stream->is_open();
}

Logger::Logger()
    : level_(static_cast<int>(LogLevel::kInfo)),
      sinks_mutex_(),
      sinks_() {
    sinks_.push_back(std::shared_ptr<ILogSink>(new ConsoleSink()));
}

Logger::~Logger() {
}

Logger& Logger::Instance() {
    static Logger logger;
    return logger;
}

void Logger::SetLevel(LogLevel level) {
    level_.store(static_cast<int>(level), std::memory_order_relaxed);
}

LogLevel Logger::GetLevel() const {
    return static_cast<LogLevel>(level_.load(std::memory_order_relaxed));
}

bool Logger::IsLevelEnabled(LogLevel level) const {
    return static_cast<int>(level) >=
           level_.load(std::memory_order_relaxed);
}

void Logger::AddSink(const std::shared_ptr<ILogSink>& sink) {
    if (!sink) {
        return;
    }

    std::lock_guard<std::mutex> lock(sinks_mutex_);
    sinks_.push_back(sink);
}

void Logger::ClearSinks() {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    sinks_.clear();
}

std::string Logger::GetTimestamp() const {
    foundation::base::Result<std::string> result =
        foundation::time::FormatLocalNow("%Y-%m-%d %H:%M:%S");

    if (result.IsOk()) {
        return result.Value();
    }

    return "1970-01-01 00:00:00";
}

void Logger::Log(LogLevel level,
                 const char* file,
                 int line,
                 const char* func,
                 const std::string& message) {
    if (!IsLevelEnabled(level)) {
        return;
    }

    std::vector<std::shared_ptr<ILogSink> > sinks_snapshot;
    {
        std::lock_guard<std::mutex> lock(sinks_mutex_);
        sinks_snapshot = sinks_;
    }

    const std::string timestamp = GetTimestamp();

    for (std::size_t i = 0; i < sinks_snapshot.size(); ++i) {
        if (sinks_snapshot[i]) {
            sinks_snapshot[i]->Write(level,
                                     timestamp.c_str(),
                                     file,
                                     line,
                                     func,
                                     message.c_str());
        }
    }
}

}  // namespace log
}  // namespace foundation