#include "foundation/perf/PerfStats.h"

#include <sstream>

namespace foundation {
namespace perf {

namespace {

double UsToMs(std::uint64_t us) {
    return static_cast<double>(us) / 1000.0;
}

}  // namespace

PerfRecord::PerfRecord()
    : count(0),
      total_us(0),
      min_us(0),
      max_us(0) {
}

PerfStats::PerfStats()
    : mutex_(),
      records_() {
}

void PerfStats::AddSample(const std::string& name, std::uint64_t duration_us) {
    std::lock_guard<std::mutex> lock(mutex_);

    PerfRecord& record = records_[name];
    record.count += 1;
    record.total_us += duration_us;

    if (record.count == 1 || duration_us < record.min_us) {
        record.min_us = duration_us;
    }
    if (record.count == 1 || duration_us > record.max_us) {
        record.max_us = duration_us;
    }
}

void PerfStats::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.clear();
}

void PerfStats::Reset(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.erase(name);
}

bool PerfStats::HasRecord(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_.find(name) != records_.end();
}

PerfRecord PerfStats::GetRecord(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::map<std::string, PerfRecord>::const_iterator it = records_.find(name);
    if (it == records_.end()) {
        return PerfRecord();
    }
    return it->second;
}

std::map<std::string, PerfRecord> PerfStats::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_;
}

std::string PerfStats::Summary(const std::string& name) const {
    PerfRecord record = GetRecord(name);
    return FormatRecord(name, record);
}

std::string PerfStats::SummaryAll() const {
    std::map<std::string, PerfRecord> snapshot = Snapshot();

    if (snapshot.empty()) {
        return "no perf data";
    }

    std::ostringstream oss;
    bool first = true;

    for (std::map<std::string, PerfRecord>::const_iterator it =
             snapshot.begin();
         it != snapshot.end();
         ++it) {
        if (!first) {
            oss << "; ";
        }
        first = false;
        oss << FormatRecord(it->first, it->second);
    }

    return oss.str();
}

std::string PerfStats::FormatRecord(const std::string& name,
                                    const PerfRecord& record) {
    std::ostringstream oss;

    if (record.count == 0) {
        oss << name << "{count=0}";
        return oss.str();
    }

    const double avg_ms =
        UsToMs(record.total_us) / static_cast<double>(record.count);

    oss.setf(std::ios::fixed);
    oss.precision(3);

    oss << name
        << "{count=" << record.count
        << ", min=" << UsToMs(record.min_us) << "ms"
        << ", max=" << UsToMs(record.max_us) << "ms"
        << ", avg=" << avg_ms << "ms"
        << ", total=" << UsToMs(record.total_us) << "ms"
        << "}";

    return oss.str();
}

}  // namespace perf
}  // namespace foundation