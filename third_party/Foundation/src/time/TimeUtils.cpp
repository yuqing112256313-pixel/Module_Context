#include "foundation/time/TimeUtils.h"

#include "foundation/base/ErrorCode.h"

#include <chrono>
#include <ctime>
#include <limits>
#include <vector>

namespace foundation {
namespace time {
namespace {

static const int64_t kMillisPerSecond = 1000;
static const std::size_t kInitialBufferSize = 64;
static const std::size_t kMaxBufferSize = 4096;

// 对负毫秒时间戳做“向下取整到秒”。
// 例如：
//   1001  -> 1
//   1000  -> 1
//   999   -> 0
//   0     -> 0
//   -1    -> -1
//   -999  -> -1
//   -1000 -> -1
//   -1001 -> -2
int64_t FloorEpochMillisToSeconds(int64_t epoch_millis) {
    int64_t seconds = epoch_millis / kMillisPerSecond;
    const int64_t remainder = epoch_millis % kMillisPerSecond;
    if (epoch_millis < 0 && remainder != 0) {
        --seconds;
    }
    return seconds;
}

bool IsTimeTInRange(int64_t epoch_seconds) {
    const int64_t min_time_t =
        static_cast<int64_t>((std::numeric_limits<std::time_t>::min)());
    const int64_t max_time_t =
        static_cast<int64_t>((std::numeric_limits<std::time_t>::max)());
    return epoch_seconds >= min_time_t && epoch_seconds <= max_time_t;
}

bool ToLocalCalendarTime(std::time_t seconds, std::tm* out_tm) {
    if (out_tm == NULL) {
        return false;
    }

#if FOUNDATION_PLATFORM_WINDOWS
    return ::localtime_s(out_tm, &seconds) == 0;
#else
    return ::localtime_r(&seconds, out_tm) != NULL;
#endif
}

bool ToUtcCalendarTime(std::time_t seconds, std::tm* out_tm) {
    if (out_tm == NULL) {
        return false;
    }

#if FOUNDATION_PLATFORM_WINDOWS
    return ::gmtime_s(out_tm, &seconds) == 0;
#else
    return ::gmtime_r(&seconds, out_tm) != NULL;
#endif
}

base::Result<std::string> FormatCalendarTime(
    const std::tm& time_value,
    const std::string& format) {
    if (format.empty()) {
        return base::ErrorCode::kInvalidArgument;
    }

    std::vector<char> buffer(kInitialBufferSize, '\0');

    while (buffer.size() <= kMaxBufferSize) {
        const std::size_t written =
            std::strftime(&buffer[0], buffer.size(), format.c_str(), &time_value);

        if (written != 0) {
            return std::string(&buffer[0], written);
        }

        if (buffer.size() == kMaxBufferSize) {
            break;
        }

        std::size_t next_size = buffer.size() * 2;
        if (next_size > kMaxBufferSize) {
            next_size = kMaxBufferSize;
        }
        buffer.resize(next_size, '\0');
    }

    // strftime 返回 0 可能是缓冲区太小，也可能是 format 不合法。
    // 这里对外统一按参数/格式无效处理。
    return base::ErrorCode::kInvalidArgument;
}

base::Result<std::string> FormatTimeInternal(
    int64_t epoch_millis,
    const std::string& format,
    bool use_utc) {
    if (format.empty()) {
        return base::ErrorCode::kInvalidArgument;
    }

    const int64_t epoch_seconds = FloorEpochMillisToSeconds(epoch_millis);
    if (!IsTimeTInRange(epoch_seconds)) {
        return base::ErrorCode::kOutOfRange;
    }

    const std::time_t seconds = static_cast<std::time_t>(epoch_seconds);
    std::tm time_value;
    const bool ok = use_utc
        ? ToUtcCalendarTime(seconds, &time_value)
        : ToLocalCalendarTime(seconds, &time_value);

    if (!ok) {
        return base::ErrorCode::kInvalidState;
    }

    return FormatCalendarTime(time_value, format);
}

}  // namespace

int64_t NowMillis() {
    const std::chrono::system_clock::duration duration =
        std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

int64_t NowMicros() {
    const std::chrono::system_clock::duration duration =
        std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

int64_t SteadyMillis() {
    const std::chrono::steady_clock::duration duration =
        std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

int64_t SteadyMicros() {
    const std::chrono::steady_clock::duration duration =
        std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

base::Result<std::string> FormatLocalTime(
    int64_t epoch_millis,
    const std::string& format) {
    return FormatTimeInternal(epoch_millis, format, false);
}

base::Result<std::string> FormatUtcTime(
    int64_t epoch_millis,
    const std::string& format) {
    return FormatTimeInternal(epoch_millis, format, true);
}

base::Result<std::string> FormatLocalNow(const std::string& format) {
    return FormatLocalTime(NowMillis(), format);
}

base::Result<std::string> FormatUtcNow(const std::string& format) {
    return FormatUtcTime(NowMillis(), format);
}

}  // namespace time
}  // namespace foundation