#ifndef FOUNDATION_PERF_SCOPEDTIMER_H_
#define FOUNDATION_PERF_SCOPEDTIMER_H_

#include <cstdint>
#include <functional>
#include <string>

#include "foundation/base/Export.h"
#include "foundation/base/NonCopyable.h"

namespace foundation {
namespace perf {

class PerfStats;

class FOUNDATION_API ScopedTimer : private foundation::base::NonCopyable {
public:
    typedef std::function<void(const std::string&, std::uint64_t)> Callback;

    explicit ScopedTimer(const std::string& name);
    ScopedTimer(const std::string& name, PerfStats& stats);
    ScopedTimer(const std::string& name, const Callback& callback);
    ScopedTimer(const std::string& name,
                PerfStats& stats,
                const Callback& callback);
    ~ScopedTimer();

    std::uint64_t ElapsedMicros() const;
    double ElapsedMillis() const;

private:
    std::string name_;
    std::int64_t start_us_;
    PerfStats* stats_;
    Callback callback_;
};

}  // namespace perf
}  // namespace foundation

#endif  // FOUNDATION_PERF_SCOPEDTIMER_H_
