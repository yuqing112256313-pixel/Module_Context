#ifndef FOUNDATION_PERF_PERFSTATS_H_
#define FOUNDATION_PERF_PERFSTATS_H_

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "foundation/base/Export.h"
#include "foundation/base/NonCopyable.h"

namespace foundation {
namespace perf {

struct FOUNDATION_API PerfRecord {
    PerfRecord();

    std::uint64_t count;
    std::uint64_t total_us;
    std::uint64_t min_us;
    std::uint64_t max_us;
};

class FOUNDATION_API PerfStats : private foundation::base::NonCopyable {
public:
    PerfStats();

    void AddSample(const std::string& name, std::uint64_t duration_us);
    void Reset();
    void Reset(const std::string& name);

    bool HasRecord(const std::string& name) const;
    PerfRecord GetRecord(const std::string& name) const;
    std::map<std::string, PerfRecord> Snapshot() const;

    std::string Summary(const std::string& name) const;
    std::string SummaryAll() const;

private:
    static std::string FormatRecord(const std::string& name,
                                    const PerfRecord& record);

private:
    mutable std::mutex mutex_;
    std::map<std::string, PerfRecord> records_;
};

}  // namespace perf
}  // namespace foundation

#endif  // FOUNDATION_PERF_PERFSTATS_H_
