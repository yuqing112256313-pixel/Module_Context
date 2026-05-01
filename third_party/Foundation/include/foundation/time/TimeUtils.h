#ifndef FOUNDATION_TIME_TIMEUTILS_H_
#define FOUNDATION_TIME_TIMEUTILS_H_

#include "foundation/base/Export.h"
#include "foundation/base/Result.h"

#include <cstdint>
#include <string>

namespace foundation {
namespace time {

// 返回当前系统时间（Unix Epoch）毫秒数。
// 适合日志、时间戳落盘、跨进程/跨机器传递。
// 不适合做耗时测量。
FOUNDATION_API int64_t NowMillis();

// 返回当前系统时间（Unix Epoch）微秒数。
// 适合更高精度的打点记录；同样不适合做耗时测量。
FOUNDATION_API int64_t NowMicros();

// 返回单调时钟毫秒数。
// 仅适合耗时统计、超时控制、重试退避。
// 该值没有日历语义，不能格式化给用户看，也不应持久化。
FOUNDATION_API int64_t SteadyMillis();

// 返回单调时钟微秒数。
// 仅适合耗时统计、超时控制、重试退避。
FOUNDATION_API int64_t SteadyMicros();

// 将 Unix Epoch 毫秒时间戳格式化为本地时间字符串。
// 注意：格式化精度为“秒”，毫秒部分会被截断，不会四舍五入。
// 常用 format 示例：
//   "%Y-%m-%d %H:%M:%S"
//   "%Y%m%d_%H%M%S"
FOUNDATION_API base::Result<std::string> FormatLocalTime(
    int64_t epoch_millis,
    const std::string& format);

// 将 Unix Epoch 毫秒时间戳格式化为 UTC 时间字符串。
// 注意：格式化精度为“秒”，毫秒部分会被截断，不会四舍五入。
FOUNDATION_API base::Result<std::string> FormatUtcTime(
    int64_t epoch_millis,
    const std::string& format);

// 按本地时间格式化当前时间。
FOUNDATION_API base::Result<std::string> FormatLocalNow(
    const std::string& format);

// 按 UTC 时间格式化当前时间。
FOUNDATION_API base::Result<std::string> FormatUtcNow(
    const std::string& format);

}  // namespace time
}  // namespace foundation

#endif  // FOUNDATION_TIME_TIMEUTILS_H_