#include "foundation/perf/ScopedTimer.h"

#include "foundation/perf/PerfStats.h"
#include "foundation/time/TimeUtils.h"

namespace foundation {
namespace perf {

ScopedTimer::ScopedTimer(const std::string& name)
    : name_(name),
      start_us_(foundation::time::SteadyMicros()),
      stats_(NULL),
      callback_() {
}

ScopedTimer::ScopedTimer(const std::string& name, PerfStats& stats)
    : name_(name),
      start_us_(foundation::time::SteadyMicros()),
      stats_(&stats),
      callback_() {
}

ScopedTimer::ScopedTimer(const std::string& name, const Callback& callback)
    : name_(name),
      start_us_(foundation::time::SteadyMicros()),
      stats_(NULL),
      callback_(callback) {
}

ScopedTimer::ScopedTimer(const std::string& name,
                         PerfStats& stats,
                         const Callback& callback)
    : name_(name),
      start_us_(foundation::time::SteadyMicros()),
      stats_(&stats),
      callback_(callback) {
}

ScopedTimer::~ScopedTimer() {
    const std::uint64_t elapsed_us = ElapsedMicros();

    if (stats_ != NULL) {
        stats_->AddSample(name_, elapsed_us);
    }

    if (callback_) {
        callback_(name_, elapsed_us);
    }
}

std::uint64_t ScopedTimer::ElapsedMicros() const {
    const std::int64_t now_us = foundation::time::SteadyMicros();
    if (now_us <= start_us_) {
        return 0;
    }
    return static_cast<std::uint64_t>(now_us - start_us_);
}

double ScopedTimer::ElapsedMillis() const {
    return static_cast<double>(ElapsedMicros()) / 1000.0;
}

}  // namespace perf
}  // namespace foundation