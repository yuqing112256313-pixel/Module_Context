#include "master_runner.h"

#include "common.h"

#include "foundation/base/ErrorCode.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using foundation::base::ErrorCode;
using foundation::base::Result;
using module_context::examples::task_flow::CreatePatternBuffer;
using module_context::examples::task_flow::EnsureDirectory;
using module_context::examples::task_flow::JoinPath;
using module_context::examples::task_flow::IMasterRunObserver;
using module_context::examples::task_flow::ISourceFrameProvider;
using module_context::examples::task_flow::MessageBusHost;
using module_context::examples::task_flow::MasterRunConfig;
using module_context::examples::task_flow::MasterRunProgress;
using module_context::examples::task_flow::MasterRunResult;
using module_context::examples::task_flow::MasterSourceFrame;
using module_context::examples::task_flow::MasterTaskSnapshot;
using module_context::examples::task_flow::ParseOptionalInt;
using module_context::examples::task_flow::ParseOptionalSize;
using module_context::examples::task_flow::ParseResultMessage;
using module_context::examples::task_flow::RequireArgument;
using module_context::examples::task_flow::ResultMessage;
using module_context::examples::task_flow::SerializeTaskMessage;
using module_context::examples::task_flow::TaskMessage;
using module_context::examples::task_flow::WaitForConnected;
using module_context::examples::task_flow::WriteTextFile;
using module_context::examples::task_flow::kResultKindData;
using module_context::examples::task_flow::kTaskKindShutdown;
using module_context::examples::task_flow::kTaskKindTask;
using module_context::http::DownloadResponse;
using module_context::http::HttpServerRequest;
using module_context::messaging::ConsumeAction;
using module_context::messaging::IncomingMessage;
using module_context::messaging::PublishRequest;

const char kTaskExchange[] = "mc.task.exchange";
const char kTaskRoutingKey[] = "task.ready";
const char kControlExchange[] = "mc.control.exchange";
const char kControlRoutingKey[] = "";
const char kImageIdHeader[] = "X-TaskFlow-Image-Id";
const char kImageTokenHeader[] = "X-TaskFlow-Image-Token";

typedef std::chrono::steady_clock SteadyClock;

Result<void> MakeError(ErrorCode code, const std::string& message) {
    return Result<void>(code, message);
}

double ElapsedMillis(
    const SteadyClock::time_point& end_time,
    const SteadyClock::time_point& begin_time) {
    return static_cast<double>(
               std::chrono::duration_cast<std::chrono::microseconds>(
                   end_time - begin_time)
                   .count()) /
           1000.0;
}

std::string FormatDouble(double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(3) << value;
    return output.str();
}

std::string SanitizeText(const std::string& value) {
    std::string sanitized = value;
    for (std::size_t index = 0; index < sanitized.size(); ++index) {
        if (sanitized[index] == '\t' || sanitized[index] == '\r' ||
            sanitized[index] == '\n') {
            sanitized[index] = ' ';
        }
    }
    return sanitized;
}

std::string GetOptionalString(
    const std::map<std::string, std::string>& args,
    const std::string& key,
    const std::string& fallback) {
    std::map<std::string, std::string>::const_iterator it = args.find(key);
    if (it == args.end() || it->second.empty()) {
        return fallback;
    }
    return it->second;
}

std::string MakeTaskId(const std::string& run_id, int source_index) {
    std::ostringstream output;
    output << run_id << "-task-" << std::setw(3) << std::setfill('0')
           << (source_index + 1);
    return output.str();
}

std::string MakeShutdownId(const std::string& run_id) {
    std::ostringstream output;
    output << run_id << "-shutdown";
    return output.str();
}

class InMemoryImageStore {
public:
    explicit InMemoryImageStore(std::size_t capacity_bytes, int ttl_ms)
        : capacity_bytes_(capacity_bytes),
          ttl_ms_(ttl_ms),
          current_bytes_(0),
          put_count_(0),
          delete_count_(0),
          sequence_(0),
          images_(),
          mutex_() {
    }

    Result<std::string> Put(
        const std::string& image_id,
        const std::shared_ptr<std::vector<char> >& frame) {
        if (image_id.empty() || !frame) {
            return Result<std::string>(
                ErrorCode::kInvalidArgument,
                "image_id or frame is empty");
        }

        std::vector<Entry> expired_entries;
        std::lock_guard<std::mutex> lock(mutex_);
        CleanupExpiredLocked(&expired_entries);
        if (images_.find(image_id) != images_.end()) {
            return Result<std::string>(
                ErrorCode::kAlreadyExists,
                "image_id already exists in image store");
        }
        if (frame->size() > capacity_bytes_ ||
            current_bytes_ + frame->size() > capacity_bytes_) {
            return Result<std::string>(
                ErrorCode::kBusy,
                "image store capacity is full");
        }

        Entry entry;
        entry.data = frame;
        entry.created_at = SteadyClock::now();
        entry.token = image_id + "-token-" +
                      std::to_string(static_cast<unsigned long long>(++sequence_));
        images_[image_id] = entry;
        current_bytes_ += frame->size();
        ++put_count_;
        return Result<std::string>(entry.token);
    }

    Result<std::shared_ptr<const std::vector<char> > > Get(
        const std::string& image_id,
        const std::string& token) {
        std::vector<Entry> expired_entries;
        std::lock_guard<std::mutex> lock(mutex_);
        CleanupExpiredLocked(&expired_entries);
        std::map<std::string, Entry>::const_iterator it = images_.find(image_id);
        if (it == images_.end()) {
            return Result<std::shared_ptr<const std::vector<char> > >(
                ErrorCode::kNotFound,
                "image_id was not found");
        }
        if (it->second.token != token) {
            return Result<std::shared_ptr<const std::vector<char> > >(
                ErrorCode::kPermissionDenied,
                "image token is invalid");
        }
        return Result<std::shared_ptr<const std::vector<char> > >(it->second.data);
    }

    Result<void> Delete(const std::string& image_id) {
        Entry removed_entry;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::map<std::string, Entry>::iterator it = images_.find(image_id);
            if (it == images_.end()) {
                return foundation::base::MakeSuccess();
            }
            removed_entry = it->second;
            current_bytes_ -= it->second.data ? it->second.data->size() : 0;
            images_.erase(it);
            ++delete_count_;
        }
        return foundation::base::MakeSuccess();
    }

    std::size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return images_.size();
    }

    std::size_t Bytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_bytes_;
    }

    std::uint64_t PutCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return put_count_;
    }

    std::uint64_t DeleteCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return delete_count_;
    }

private:
    struct Entry {
        std::shared_ptr<std::vector<char> > data;
        std::string token;
        SteadyClock::time_point created_at;
    };

    void CleanupExpiredLocked(std::vector<Entry>* expired_entries) {
        if (ttl_ms_ <= 0) {
            return;
        }
        const SteadyClock::time_point now = SteadyClock::now();
        std::map<std::string, Entry>::iterator it = images_.begin();
        while (it != images_.end()) {
            if (ElapsedMillis(now, it->second.created_at) <=
                static_cast<double>(ttl_ms_)) {
                ++it;
                continue;
            }
            if (expired_entries != NULL) {
                expired_entries->push_back(it->second);
            }
            current_bytes_ -= it->second.data ? it->second.data->size() : 0;
            images_.erase(it++);
            ++delete_count_;
        }
    }

    std::size_t capacity_bytes_;
    int ttl_ms_;
    std::size_t current_bytes_;
    std::uint64_t put_count_;
    std::uint64_t delete_count_;
    std::uint64_t sequence_;
    std::map<std::string, Entry> images_;
    mutable std::mutex mutex_;
};

struct TaskState {
    int source_index;
    std::string task_id;
    std::string image_id;
    std::string worker_id;
    std::string status;
    std::string detail_message;
    std::string image_store_status;
    std::string image_store_message;
    std::string error_message;
    std::string source_path;
    std::string source_name;
    std::size_t image_bytes;
    std::size_t processed_bytes;
    double source_offset_ms;
    double source_interval_ms;
    double source_stage_ms;
    double task_queue_ms;
    double image_store_put_ms;
    double publish_ms;
    double worker_queue_ms;
    double image_fetch_ms;
    double http_setup_ms;
    double http_header_wait_ms;
    double http_first_byte_ms;
    double http_body_ms;
    double http_chunk_callback_ms;
    double http_total_ms;
    std::size_t http_chunk_count;
    double algorithm_ms;
    double worker_publish_ms;
    double image_store_delete_ms;
    double worker_total_ms;
    double result_queue_ms;
    double result_handle_ms;
    double master_end_to_end_ms;
    double residual_ms;
    double completion_offset_ms;
    bool publish_ok;
    bool finished;
    bool result_received;
    SteadyClock::time_point source_time;

    TaskState()
        : source_index(-1),
          task_id(),
          image_id(),
          worker_id(),
          status("pending"),
          detail_message(),
          image_store_status("not_stored"),
          image_store_message(),
          error_message(),
          source_path(),
          source_name(),
          image_bytes(0),
          processed_bytes(0),
          source_offset_ms(0.0),
          source_interval_ms(0.0),
          source_stage_ms(0.0),
          task_queue_ms(0.0),
          image_store_put_ms(0.0),
          publish_ms(0.0),
          worker_queue_ms(0.0),
          image_fetch_ms(0.0),
          http_setup_ms(0.0),
          http_header_wait_ms(0.0),
          http_first_byte_ms(0.0),
          http_body_ms(0.0),
          http_chunk_callback_ms(0.0),
          http_total_ms(0.0),
          http_chunk_count(0),
          algorithm_ms(0.0),
          worker_publish_ms(0.0),
          image_store_delete_ms(0.0),
          worker_total_ms(0.0),
          result_queue_ms(0.0),
          result_handle_ms(0.0),
          master_end_to_end_ms(0.0),
          residual_ms(0.0),
          completion_offset_ms(0.0),
          publish_ok(false),
          finished(false),
          result_received(false),
          source_time() {
    }
};

struct SharedState {
    explicit SharedState(int count)
        : image_lookup(),
          tasks(static_cast<std::size_t>(count)),
          finished_count(0),
          sent_count(0),
          success_count(0),
          failure_count(0),
          cleanup_failure_count(0),
          result_count(0),
          late_result_ack_only(false),
          run_start(),
          last_result_activity(),
          mutex(),
          completion_cv() {
    }

    std::map<std::string, int> image_lookup;
    std::vector<TaskState> tasks;
    int finished_count;
    int sent_count;
    int success_count;
    int failure_count;
    int cleanup_failure_count;
    int result_count;
    bool late_result_ack_only;
    SteadyClock::time_point run_start;
    SteadyClock::time_point last_result_activity;
    std::mutex mutex;
    std::condition_variable completion_cv;
};

bool IsCancellationRequested(const std::atomic_bool* cancel_requested) {
    return cancel_requested != NULL && cancel_requested->load();
}

MasterTaskSnapshot MakeTaskSnapshot(const TaskState& task) {
    MasterTaskSnapshot snapshot;
    snapshot.source_index = task.source_index;
    snapshot.task_id = task.task_id;
    snapshot.image_id = task.image_id;
    snapshot.worker_id = task.worker_id;
    snapshot.status = task.status;
    snapshot.detail_message = task.detail_message;
    snapshot.image_store_status = task.image_store_status;
    snapshot.error_message = task.error_message;
    snapshot.source_path = task.source_path;
    snapshot.source_name = task.source_name;
    snapshot.image_bytes = task.image_bytes;
    snapshot.processed_bytes = task.processed_bytes;
    snapshot.source_offset_ms = task.source_offset_ms;
    snapshot.source_interval_ms = task.source_interval_ms;
    snapshot.source_stage_ms = task.source_stage_ms;
    snapshot.task_queue_ms = task.task_queue_ms;
    snapshot.image_store_put_ms = task.image_store_put_ms;
    snapshot.publish_ms = task.publish_ms;
    snapshot.worker_queue_ms = task.worker_queue_ms;
    snapshot.image_fetch_ms = task.image_fetch_ms;
    snapshot.http_setup_ms = task.http_setup_ms;
    snapshot.http_header_wait_ms = task.http_header_wait_ms;
    snapshot.http_first_byte_ms = task.http_first_byte_ms;
    snapshot.http_body_ms = task.http_body_ms;
    snapshot.http_chunk_callback_ms = task.http_chunk_callback_ms;
    snapshot.http_total_ms = task.http_total_ms;
    snapshot.http_chunk_count = task.http_chunk_count;
    snapshot.algorithm_ms = task.algorithm_ms;
    snapshot.worker_publish_ms = task.worker_publish_ms;
    snapshot.image_store_delete_ms = task.image_store_delete_ms;
    snapshot.worker_total_ms = task.worker_total_ms;
    snapshot.result_queue_ms = task.result_queue_ms;
    snapshot.result_handle_ms = task.result_handle_ms;
    snapshot.master_end_to_end_ms = task.master_end_to_end_ms;
    snapshot.residual_ms = task.residual_ms;
    snapshot.completion_offset_ms = task.completion_offset_ms;
    snapshot.publish_ok = task.publish_ok;
    snapshot.finished = task.finished;
    snapshot.result_received = task.result_received;
    return snapshot;
}

MasterRunProgress MakeProgressSnapshot(const SharedState& shared_state) {
    MasterRunProgress progress;
    progress.sent_count = shared_state.sent_count;
    progress.finished_count = shared_state.finished_count;
    progress.success_count = shared_state.success_count;
    progress.failure_count = shared_state.failure_count;
    progress.cleanup_failure_count = shared_state.cleanup_failure_count;
    progress.result_count = shared_state.result_count;
    progress.task_count = static_cast<int>(shared_state.tasks.size());
    if (shared_state.run_start.time_since_epoch().count() != 0) {
        progress.elapsed_ms = ElapsedMillis(SteadyClock::now(), shared_state.run_start);
    }
    return progress;
}

void NotifyLog(
    IMasterRunObserver* observer,
    const std::string& level,
    const std::string& message) {
    if (observer != NULL) {
        observer->OnLog(level, message);
    }
}

void NotifyTaskPublished(
    IMasterRunObserver* observer,
    const MasterTaskSnapshot& snapshot) {
    if (observer != NULL) {
        observer->OnTaskPublished(snapshot);
    }
}

void NotifyTaskUpdated(
    IMasterRunObserver* observer,
    const MasterTaskSnapshot& snapshot) {
    if (observer != NULL) {
        observer->OnTaskUpdated(snapshot);
    }
}

void NotifyProgress(
    IMasterRunObserver* observer,
    const MasterRunProgress& progress) {
    if (observer != NULL) {
        observer->OnProgress(progress);
    }
}

double ComputeResidualMs(const TaskState& task) {
    const double accounted =
        task.source_stage_ms + task.task_queue_ms + task.image_store_put_ms +
        task.publish_ms + task.worker_queue_ms + task.image_fetch_ms +
        task.algorithm_ms + task.worker_publish_ms +
        task.image_store_delete_ms + task.result_queue_ms +
        task.result_handle_ms;
    const double residual = task.master_end_to_end_ms - accounted;
    return residual < 0.0 ? 0.0 : residual;
}

void FinalizeTask(
    SharedState* shared_state,
    IMasterRunObserver* observer,
    int task_index,
    const std::string& status,
    const std::string& error_message,
    const SteadyClock::time_point& finished_time) {
    if (shared_state == NULL) {
        return;
    }

    MasterTaskSnapshot task_snapshot;
    MasterRunProgress progress_snapshot;
    {
        std::unique_lock<std::mutex> lock(shared_state->mutex);
        TaskState& task = shared_state->tasks[static_cast<std::size_t>(task_index)];
        if (task.finished) {
            return;
        }

        task.status = status;
        task.error_message = error_message;
        if (task.detail_message.empty()) {
            task.detail_message = error_message;
        }
        if (task.source_time.time_since_epoch().count() == 0) {
            task.source_time = finished_time;
        }
        task.finished = true;
        task.completion_offset_ms =
            ElapsedMillis(finished_time, shared_state->run_start);
        task.master_end_to_end_ms = ElapsedMillis(finished_time, task.source_time);
        task.residual_ms = ComputeResidualMs(task);

        ++shared_state->finished_count;
        if (status == "processed") {
            ++shared_state->success_count;
        } else {
            ++shared_state->failure_count;
        }
        task_snapshot = MakeTaskSnapshot(task);
        progress_snapshot = MakeProgressSnapshot(*shared_state);
        shared_state->completion_cv.notify_all();
    }

    NotifyTaskUpdated(observer, task_snapshot);
    NotifyProgress(observer, progress_snapshot);
}

Result<void> WriteTaskMetrics(
    const MasterRunConfig& config,
    const SharedState& shared_state) {
    std::ostringstream output;
    output << "task_id\timage_id\tsource_index\tstatus\tworker_id\timage_bytes\t"
           << "processed_bytes\tsource_offset_ms\tsource_interval_ms\t"
           << "source_stage_ms\ttask_queue_ms\timage_store_put_ms\t"
           << "publish_ms\tworker_queue_ms\timage_fetch_ms\t"
           << "http_setup_ms\thttp_header_wait_ms\thttp_first_byte_ms\t"
           << "http_body_ms\thttp_chunk_callback_ms\thttp_total_ms\t"
           << "http_chunk_count\talgorithm_ms\t"
           << "worker_publish_ms\timage_store_delete_ms\tworker_total_ms\t"
           << "result_queue_ms\tresult_handle_ms\tmaster_end_to_end_ms\t"
           << "residual_ms\tcompletion_offset_ms\timage_store_status\t"
           << "detail_message\timage_store_message\terror_message\n";

    for (std::size_t index = 0; index < shared_state.tasks.size(); ++index) {
        const TaskState& task = shared_state.tasks[index];
        output << SanitizeText(task.task_id) << "\t"
               << SanitizeText(task.image_id) << "\t"
               << task.source_index << "\t"
               << SanitizeText(task.status) << "\t"
               << SanitizeText(task.worker_id) << "\t"
               << static_cast<unsigned long long>(task.image_bytes) << "\t"
               << static_cast<unsigned long long>(task.processed_bytes) << "\t"
               << FormatDouble(task.source_offset_ms) << "\t"
               << FormatDouble(task.source_interval_ms) << "\t"
               << FormatDouble(task.source_stage_ms) << "\t"
               << FormatDouble(task.task_queue_ms) << "\t"
               << FormatDouble(task.image_store_put_ms) << "\t"
               << FormatDouble(task.publish_ms) << "\t"
               << FormatDouble(task.worker_queue_ms) << "\t"
               << FormatDouble(task.image_fetch_ms) << "\t"
               << FormatDouble(task.http_setup_ms) << "\t"
               << FormatDouble(task.http_header_wait_ms) << "\t"
               << FormatDouble(task.http_first_byte_ms) << "\t"
               << FormatDouble(task.http_body_ms) << "\t"
               << FormatDouble(task.http_chunk_callback_ms) << "\t"
               << FormatDouble(task.http_total_ms) << "\t"
               << static_cast<unsigned long long>(task.http_chunk_count) << "\t"
               << FormatDouble(task.algorithm_ms) << "\t"
               << FormatDouble(task.worker_publish_ms) << "\t"
               << FormatDouble(task.image_store_delete_ms) << "\t"
               << FormatDouble(task.worker_total_ms) << "\t"
               << FormatDouble(task.result_queue_ms) << "\t"
               << FormatDouble(task.result_handle_ms) << "\t"
               << FormatDouble(task.master_end_to_end_ms) << "\t"
               << FormatDouble(task.residual_ms) << "\t"
               << FormatDouble(task.completion_offset_ms) << "\t"
               << SanitizeText(task.image_store_status) << "\t"
               << SanitizeText(task.detail_message) << "\t"
               << SanitizeText(task.image_store_message) << "\t"
               << SanitizeText(task.error_message) << "\n";
    }

    return WriteTextFile(
        JoinPath(config.report_dir, "task_metrics.tsv"),
        output.str());
}

Result<void> WriteSummary(
    const MasterRunConfig& config,
    const SharedState& shared_state,
    const InMemoryImageStore& image_store) {
    std::ostringstream output;
    output << "run_id=" << config.run_id << "\n";
    output << "profile_name=" << config.profile_name << "\n";
    output << "task_count=" << config.task_count << "\n";
    output << "publish_rate=" << config.publish_rate << "\n";
    output << "image_size_bytes="
           << static_cast<unsigned long long>(config.image_size_bytes) << "\n";
    output << "worker_count=" << config.worker_count << "\n";
    output << "master_write_publish_threads="
           << config.master_write_publish_threads << "\n";
    output << "master_result_threads=" << config.master_result_threads << "\n";
    output << "http_chunk_bytes="
           << static_cast<unsigned long long>(config.http_chunk_bytes) << "\n";
    output << "http_read_buffer_bytes="
           << static_cast<unsigned long long>(config.http_read_buffer_bytes)
           << "\n";
    output << "http_write_buffer_bytes="
           << static_cast<unsigned long long>(config.http_write_buffer_bytes)
           << "\n";
    output << "http_socket_receive_buffer_bytes="
           << static_cast<unsigned long long>(
                  config.http_socket_receive_buffer_bytes)
           << "\n";
    output << "http_socket_send_buffer_bytes="
           << static_cast<unsigned long long>(
                  config.http_socket_send_buffer_bytes)
           << "\n";
    output << "send_shutdown=" << (config.send_shutdown ? 1 : 0) << "\n";
    output << "http_route=" << config.http_route << "\n";
    output << "source_buffer_mode=" << config.source_buffer_mode << "\n";
    output << "source_prepare_ms=" << FormatDouble(config.source_prepare_ms)
           << "\n";
    output << "task_queue=mc.task.queue\n";
    output << "task_routing_key=" << kTaskRoutingKey << "\n";
    output << "control_exchange=" << kControlExchange << "\n";
    output << "sent_count=" << shared_state.sent_count << "\n";
    output << "completed_count=" << shared_state.finished_count << "\n";
    output << "success_count=" << shared_state.success_count << "\n";
    output << "failure_count=" << shared_state.failure_count << "\n";
    output << "cleanup_failure_count=" << shared_state.cleanup_failure_count << "\n";
    output << "result_count=" << shared_state.result_count << "\n";
    output << "image_store_remaining_count=" << image_store.Size() << "\n";
    output << "image_store_remaining_bytes="
           << static_cast<unsigned long long>(image_store.Bytes()) << "\n";
    output << "image_store_put_count="
           << static_cast<unsigned long long>(image_store.PutCount()) << "\n";
    output << "image_store_delete_count="
           << static_cast<unsigned long long>(image_store.DeleteCount()) << "\n";
    return WriteTextFile(
        JoinPath(config.report_dir, "master_summary.txt"),
        output.str());
}

PublishRequest BuildPublishRequest(
    const TaskMessage& task_message,
    const std::string& exchange,
    const std::string& routing_key) {
    const std::string payload_text = SerializeTaskMessage(task_message);
    PublishRequest request;
    request.exchange = exchange;
    request.routing_key = routing_key;
    request.content_type = "text/plain";
    request.correlation_id = task_message.task_id;
    request.persistent = false;
    request.mandatory = false;
    request.payload.assign(payload_text.begin(), payload_text.end());
    return request;
}

Result<void> PublishTaskMessage(
    module_context::messaging::IMessageBusService* bus,
    const TaskMessage& task_message) {
    if (bus == NULL) {
        return MakeError(ErrorCode::kInvalidArgument, "IMessageBusService is null");
    }

    PublishRequest request =
        BuildPublishRequest(task_message, kTaskExchange, kTaskRoutingKey);
    return bus->PublishAsync(request);
}

Result<void> PublishControlMessage(
    module_context::messaging::IMessageBusService* bus,
    const TaskMessage& task_message) {
    if (bus == NULL) {
        return MakeError(ErrorCode::kInvalidArgument, "IMessageBusService is null");
    }

    PublishRequest request =
        BuildPublishRequest(task_message, kControlExchange, kControlRoutingKey);
    return bus->PublishAsync(request);
}

DownloadResponse BuildImageDownloadResponse(
    const std::shared_ptr<const std::vector<char> >& data,
    std::size_t chunk_bytes) {
    DownloadResponse response;
    response.content_type = "application/octet-stream";
    response.content_length = data ? data->size() : 0;
    response.buffer_reader =
        [data, chunk_bytes](
            std::uint64_t offset,
            std::size_t max_bytes,
            const char** buffer,
            std::size_t* size,
            bool* eof) -> Result<void> {
            if (!data || buffer == NULL || size == NULL || eof == NULL) {
                return MakeError(ErrorCode::kInvalidArgument, "invalid read request");
            }
            if (offset >= data->size()) {
                *buffer = NULL;
                *size = 0;
                *eof = true;
                return foundation::base::MakeSuccess();
            }
            const std::size_t available =
                data->size() - static_cast<std::size_t>(offset);
            const std::size_t limit =
                std::min<std::size_t>(
                    std::min<std::size_t>(max_bytes, chunk_bytes),
                    available);
            *buffer = &(*data)[static_cast<std::size_t>(offset)];
            *size = limit;
            *eof = offset + limit >= data->size();
            return foundation::base::MakeSuccess();
        };
    return response;
}

ConsumeAction HandleResultMessage(
    const std::shared_ptr<SharedState>& shared_state,
    const std::shared_ptr<InMemoryImageStore>& image_store,
    IMasterRunObserver* observer,
    const ResultMessage& result_message,
    const SteadyClock::time_point& received_time) {
    if (!shared_state || !image_store) {
        return ConsumeAction::Requeue;
    }

    const SteadyClock::time_point handle_begin = SteadyClock::now();
    int task_index = -1;
    {
        std::unique_lock<std::mutex> lock(shared_state->mutex);
        shared_state->last_result_activity = received_time;
        if (shared_state->late_result_ack_only) {
            shared_state->completion_cv.notify_all();
            return ConsumeAction::Ack;
        }

        std::map<std::string, int>::const_iterator lookup_it =
            shared_state->image_lookup.find(result_message.image_id);
        if (lookup_it == shared_state->image_lookup.end()) {
            std::cerr << "master ignored result for unknown image '"
                      << result_message.image_id << "'" << std::endl;
            shared_state->completion_cv.notify_all();
            return ConsumeAction::Ack;
        }

        task_index = lookup_it->second;
        TaskState& task =
            shared_state->tasks[static_cast<std::size_t>(task_index)];
        if (task.result_received) {
            shared_state->completion_cv.notify_all();
            return ConsumeAction::Ack;
        }

        task.worker_id = result_message.worker_id;
        task.status = result_message.status;
        task.detail_message = result_message.detail_message;
        task.processed_bytes = result_message.processed_bytes;
        task.worker_queue_ms = result_message.worker_queue_ms;
        task.image_fetch_ms = result_message.image_fetch_ms;
        task.http_setup_ms = result_message.http_setup_ms;
        task.http_header_wait_ms = result_message.http_header_wait_ms;
        task.http_first_byte_ms = result_message.http_first_byte_ms;
        task.http_body_ms = result_message.http_body_ms;
        task.http_chunk_callback_ms = result_message.http_chunk_callback_ms;
        task.http_total_ms = result_message.http_total_ms;
        task.http_chunk_count = result_message.http_chunk_count;
        task.algorithm_ms = result_message.algorithm_ms;
        task.worker_publish_ms = result_message.worker_publish_ms;
        task.worker_total_ms = result_message.worker_total_ms;
        task.result_queue_ms = ElapsedMillis(handle_begin, received_time);
    }

    const SteadyClock::time_point delete_begin = SteadyClock::now();
    Result<void> delete_result = image_store->Delete(result_message.image_id);
    const SteadyClock::time_point delete_end = SteadyClock::now();
    const double delete_ms = ElapsedMillis(delete_end, delete_begin);

    const SteadyClock::time_point handle_end = SteadyClock::now();
    const double result_handle_ms = ElapsedMillis(handle_end, handle_begin);

    MasterTaskSnapshot task_snapshot;
    MasterRunProgress progress_snapshot;
    int finished_count = 0;
    int task_count = 0;
    {
        std::unique_lock<std::mutex> lock(shared_state->mutex);
        TaskState& task = shared_state->tasks[static_cast<std::size_t>(task_index)];
        if (task.finished) {
            shared_state->completion_cv.notify_all();
            return ConsumeAction::Ack;
        }

        task.result_handle_ms = result_handle_ms;
        task.image_store_delete_ms = delete_ms;
        task.result_received = true;
        task.finished = true;
        task.completion_offset_ms =
            ElapsedMillis(delete_end, shared_state->run_start);
        task.master_end_to_end_ms = ElapsedMillis(delete_end, task.source_time);
        if (!result_message.detail_message.empty()) {
            task.error_message = result_message.detail_message;
        }
        if (delete_result.IsOk()) {
            task.image_store_status = "deleted";
        } else {
            task.image_store_status = "delete_failed";
            task.image_store_message = delete_result.GetMessage();
            ++shared_state->cleanup_failure_count;
        }
        task.residual_ms = ComputeResidualMs(task);

        ++shared_state->finished_count;
        ++shared_state->result_count;
        if (task.status == "processed") {
            ++shared_state->success_count;
        } else {
            ++shared_state->failure_count;
        }
        finished_count = shared_state->finished_count;
        task_count = static_cast<int>(shared_state->tasks.size());
        task_snapshot = MakeTaskSnapshot(task);
        progress_snapshot = MakeProgressSnapshot(*shared_state);
        shared_state->completion_cv.notify_all();
    }
    NotifyTaskUpdated(observer, task_snapshot);
    NotifyProgress(observer, progress_snapshot);
    if (finished_count % 10 == 0 || finished_count == task_count) {
        std::ostringstream message;
        message << "[master] progress " << finished_count << "/" << task_count;
        std::cout << message.str() << std::endl;
        NotifyLog(observer, "info", message.str());
    }
    return ConsumeAction::Ack;
}

void CleanupTimedOutTask(
    const std::shared_ptr<SharedState>& shared_state,
    const std::shared_ptr<InMemoryImageStore>& image_store,
    int task_index) {
    std::string image_id;
    {
        std::unique_lock<std::mutex> lock(shared_state->mutex);
        image_id = shared_state->tasks[static_cast<std::size_t>(task_index)].image_id;
    }
    if (image_id.empty()) {
        return;
    }

    const SteadyClock::time_point delete_begin = SteadyClock::now();
    Result<void> delete_result = image_store->Delete(image_id);
    const SteadyClock::time_point delete_end = SteadyClock::now();

    std::unique_lock<std::mutex> lock(shared_state->mutex);
    TaskState& task = shared_state->tasks[static_cast<std::size_t>(task_index)];
    task.image_store_delete_ms = ElapsedMillis(delete_end, delete_begin);
    if (delete_result.IsOk()) {
        task.image_store_status = "deleted";
    } else {
        task.image_store_status = "delete_failed";
        task.image_store_message = delete_result.GetMessage();
        ++shared_state->cleanup_failure_count;
    }
}

}  // namespace

namespace module_context {
namespace examples {
namespace task_flow {

MasterRunConfig::MasterRunConfig()
    : module_config_path(),
      run_id(),
      report_dir(),
      profile_name("single"),
      http_route("/task-flow/images"),
      task_count(100),
      publish_rate(50),
      image_size_bytes(20971520),
      image_store_capacity_bytes(4294967296ULL),
      image_store_ttl_ms(600000),
      master_write_publish_threads(4),
      master_result_threads(4),
      worker_count(5),
      timeout_ms(180000),
      http_chunk_bytes(8388608),
      http_read_buffer_bytes(0),
      http_write_buffer_bytes(0),
      http_socket_receive_buffer_bytes(0),
      http_socket_send_buffer_bytes(0),
      send_shutdown(true),
      source_buffer_mode("reused_prebuilt"),
      source_prepare_ms(0.0) {
}

MasterSourceFrame::MasterSourceFrame()
    : data(),
      source_path(),
      display_name() {
}

ISourceFrameProvider::~ISourceFrameProvider() {
}

PatternFrameProvider::PatternFrameProvider()
    : frame_(),
      source_prepare_ms_(0.0) {
}

Result<void> PatternFrameProvider::Prepare(
    std::size_t fallback_image_size_bytes) {
    const SteadyClock::time_point source_prepare_begin = SteadyClock::now();
    frame_.reset(
        new std::vector<char>(
            CreatePatternBuffer(
                fallback_image_size_bytes,
                static_cast<std::uint32_t>(1))));
    const SteadyClock::time_point source_prepare_end = SteadyClock::now();
    source_prepare_ms_ =
        ElapsedMillis(source_prepare_end, source_prepare_begin);
    return foundation::base::MakeSuccess();
}

Result<MasterSourceFrame> PatternFrameProvider::GetFrame(int /*source_index*/) {
    if (!frame_) {
        return Result<MasterSourceFrame>(
            ErrorCode::kInvalidState,
            "pattern source frame was not prepared");
    }
    MasterSourceFrame frame;
    frame.data = frame_;
    frame.display_name = "pattern";
    return Result<MasterSourceFrame>(frame);
}

std::string PatternFrameProvider::SourceBufferMode() const {
    return "reused_prebuilt";
}

double PatternFrameProvider::SourcePrepareMs() const {
    return source_prepare_ms_;
}

MasterTaskSnapshot::MasterTaskSnapshot()
    : source_index(-1),
      task_id(),
      image_id(),
      worker_id(),
      status("pending"),
      detail_message(),
      image_store_status("not_stored"),
      error_message(),
      source_path(),
      source_name(),
      image_bytes(0),
      processed_bytes(0),
      source_offset_ms(0.0),
      source_interval_ms(0.0),
      source_stage_ms(0.0),
      task_queue_ms(0.0),
      image_store_put_ms(0.0),
      publish_ms(0.0),
      worker_queue_ms(0.0),
      image_fetch_ms(0.0),
      http_setup_ms(0.0),
      http_header_wait_ms(0.0),
      http_first_byte_ms(0.0),
      http_body_ms(0.0),
      http_chunk_callback_ms(0.0),
      http_total_ms(0.0),
      http_chunk_count(0),
      algorithm_ms(0.0),
      worker_publish_ms(0.0),
      image_store_delete_ms(0.0),
      worker_total_ms(0.0),
      result_queue_ms(0.0),
      result_handle_ms(0.0),
      master_end_to_end_ms(0.0),
      residual_ms(0.0),
      completion_offset_ms(0.0),
      publish_ok(false),
      finished(false),
      result_received(false) {
}

MasterRunProgress::MasterRunProgress()
    : sent_count(0),
      finished_count(0),
      success_count(0),
      failure_count(0),
      cleanup_failure_count(0),
      result_count(0),
      task_count(0),
      elapsed_ms(0.0) {
}

MasterRunResult::MasterRunResult()
    : success(false),
      timed_out(false),
      cancelled(false),
      exit_code(1),
      sent_count(0),
      finished_count(0),
      success_count(0),
      failure_count(0),
      cleanup_failure_count(0),
      result_count(0),
      image_store_remaining_count(0),
      image_store_remaining_bytes(0) {
}

IMasterRunObserver::~IMasterRunObserver() {
}

void IMasterRunObserver::OnLog(
    const std::string& /*level*/,
    const std::string& /*message*/) {
}

void IMasterRunObserver::OnTaskPublished(
    const MasterTaskSnapshot& /*task*/) {
}

void IMasterRunObserver::OnTaskUpdated(
    const MasterTaskSnapshot& /*task*/) {
}

void IMasterRunObserver::OnProgress(
    const MasterRunProgress& /*progress*/) {
}

void IMasterRunObserver::OnRunFinished(
    const MasterRunResult& /*result*/) {
}

Result<MasterRunConfig> ParseMasterRunConfigFromArgs(
    const std::map<std::string, std::string>& args) {
    Result<std::string> module_config = RequireArgument(args, "module-config");
    Result<std::string> run_id = RequireArgument(args, "run-id");
    Result<std::string> report_dir = RequireArgument(args, "report-dir");
    if (!module_config.IsOk() || !run_id.IsOk() || !report_dir.IsOk()) {
        return Result<MasterRunConfig>(
            ErrorCode::kInvalidArgument,
            module_config.IsOk()
                ? (run_id.IsOk() ? report_dir.GetMessage() : run_id.GetMessage())
                : module_config.GetMessage());
    }

    MasterRunConfig config;
    config.module_config_path = module_config.Value();
    config.run_id = run_id.Value();
    config.report_dir = report_dir.Value();
    config.profile_name = GetOptionalString(args, "profile-name", "single");
    config.http_route = GetOptionalString(args, "http-route", "/task-flow/images");
    config.task_count = ParseOptionalInt(args, "task-count", 100);
    config.publish_rate = ParseOptionalInt(args, "publish-rate", 50);
    config.image_size_bytes = ParseOptionalSize(args, "image-size-bytes", 20971520);
    config.image_store_capacity_bytes =
        ParseOptionalSize(args, "image-store-capacity-bytes", 4294967296ULL);
    config.image_store_ttl_ms =
        ParseOptionalInt(args, "image-store-ttl-ms", 600000);
    config.master_write_publish_threads =
        ParseOptionalInt(args, "master-write-publish-threads", 4);
    config.master_result_threads =
        ParseOptionalInt(args, "master-result-threads", 4);
    config.worker_count = ParseOptionalInt(args, "worker-count", 5);
    config.timeout_ms = ParseOptionalInt(args, "timeout-ms", 180000);
    config.http_chunk_bytes =
        ParseOptionalSize(args, "http-chunk-bytes", 8388608);
    config.http_read_buffer_bytes =
        ParseOptionalSize(args, "http-read-buffer-bytes", 0);
    config.http_write_buffer_bytes =
        ParseOptionalSize(args, "http-write-buffer-bytes", 0);
    config.http_socket_receive_buffer_bytes =
        ParseOptionalSize(args, "http-socket-receive-buffer-bytes", 0);
    config.http_socket_send_buffer_bytes =
        ParseOptionalSize(args, "http-socket-send-buffer-bytes", 0);
    config.send_shutdown = ParseOptionalInt(args, "send-shutdown", 1) != 0;
    config.source_buffer_mode = "reused_prebuilt";
    config.source_prepare_ms = 0.0;

    if (config.task_count <= 0 || config.publish_rate <= 0 ||
        config.image_size_bytes == 0 ||
        config.image_store_capacity_bytes == 0 ||
        config.image_store_ttl_ms <= 0 ||
        config.master_write_publish_threads <= 0 ||
        config.master_result_threads <= 0 || config.worker_count <= 0 ||
        config.timeout_ms < 0 || config.http_chunk_bytes == 0) {
        return Result<MasterRunConfig>(
            ErrorCode::kInvalidArgument,
            "master received invalid numeric arguments");
    }

    return Result<MasterRunConfig>(config);
}

int RunTaskFlowMaster(
    const MasterRunConfig& input_config,
    ISourceFrameProvider* source_provider,
    IMasterRunObserver* observer,
    std::atomic_bool* cancel_requested) {
    MasterRunConfig config = input_config;

    Result<void> ensure_report_dir = EnsureDirectory(config.report_dir);
    if (!ensure_report_dir.IsOk()) {
        std::cerr << ensure_report_dir.GetMessage() << std::endl;
        return 1;
    }

    MessageBusHost host(config.module_config_path);
    Result<void> load_result = host.LoadModules();
    if (!load_result.IsOk()) {
        std::cerr << "master load modules failed: " << load_result.GetMessage()
                  << std::endl;
        return 1;
    }

    Result<void> init_result = host.Init();
    if (!init_result.IsOk()) {
        std::cerr << "master init failed: " << init_result.GetMessage() << std::endl;
        (void)host.Fini();
        return 1;
    }

    std::shared_ptr<InMemoryImageStore> image_store(
        new InMemoryImageStore(
            config.image_store_capacity_bytes,
            config.image_store_ttl_ms));

    Result<module_context::http::IHttpTransferService*> http_result =
        host.HttpService();
    if (!http_result.IsOk()) {
        std::cerr << "master http service unavailable: "
                  << http_result.GetMessage() << std::endl;
        (void)host.Fini();
        return 1;
    }
    module_context::http::IHttpTransferService* http = http_result.Value();
    Result<void> register_http = http->RegisterDownloadHandler(
        config.http_route,
        [image_store, config](const HttpServerRequest& request)
            -> Result<DownloadResponse> {
            std::map<std::string, std::string>::const_iterator image_id_it =
                request.headers.find(kImageIdHeader);
            std::map<std::string, std::string>::const_iterator token_it =
                request.headers.find(kImageTokenHeader);
            if (image_id_it == request.headers.end() ||
                token_it == request.headers.end()) {
                return Result<DownloadResponse>(
                    ErrorCode::kInvalidArgument,
                    "image id or token header is missing");
            }
            Result<std::shared_ptr<const std::vector<char> > > image =
                image_store->Get(image_id_it->second, token_it->second);
            if (!image.IsOk()) {
                return Result<DownloadResponse>(
                    image.GetError(),
                    image.GetMessage());
            }
            return Result<DownloadResponse>(
                BuildImageDownloadResponse(
                    image.Value(),
                    config.http_chunk_bytes));
        });
    if (!register_http.IsOk()) {
        std::cerr << "master failed to register HTTP image route: "
                  << register_http.GetMessage() << std::endl;
        (void)host.Fini();
        return 1;
    }

    std::shared_ptr<SharedState> shared_state(new SharedState(config.task_count));

    Result<void> start_result = host.Start();
    if (!start_result.IsOk()) {
        std::cerr << "master start failed: " << start_result.GetMessage() << std::endl;
        (void)http->UnregisterHandler(config.http_route);
        (void)host.Fini();
        return 1;
    }

    Result<module_context::messaging::IMessageBusService*> bus_result =
        host.BusService();
    if (!bus_result.IsOk()) {
        std::cerr << "master bus service unavailable: " << bus_result.GetMessage()
                  << std::endl;
        (void)http->UnregisterHandler(config.http_route);
        (void)host.Stop();
        (void)host.Fini();
        return 1;
    }
    module_context::messaging::IMessageBusService* bus = bus_result.Value();

    Result<void> register_result = bus->RegisterConsumerHandler(
        "result_consumer",
        [shared_state, image_store, observer](const IncomingMessage& incoming) {
            const std::string payload(incoming.payload.begin(), incoming.payload.end());
            Result<ResultMessage> parsed = ParseResultMessage(payload);
            if (!parsed.IsOk()) {
                std::cerr << "master failed to parse result payload: "
                          << parsed.GetMessage() << std::endl;
                return ConsumeAction::Reject;
            }
            if (parsed.Value().kind != kResultKindData) {
                return ConsumeAction::Reject;
            }
            return HandleResultMessage(
                shared_state,
                image_store,
                observer,
                parsed.Value(),
                SteadyClock::now());
        });
    if (!register_result.IsOk()) {
        std::cerr << "master failed to register result consumer: "
                  << register_result.GetMessage() << std::endl;
        (void)http->UnregisterHandler(config.http_route);
        (void)host.Stop();
        (void)host.Fini();
        return 1;
    }

    Result<void> wait_result = WaitForConnected(bus, config.timeout_ms);
    if (!wait_result.IsOk()) {
        std::cerr << "master failed to connect RabbitMQ: " << wait_result.GetMessage()
                  << std::endl;
        (void)bus->UnregisterConsumerHandler("result_consumer");
        (void)http->UnregisterHandler(config.http_route);
        (void)host.Stop();
        (void)host.Fini();
        return 1;
    }

    PatternFrameProvider default_source_provider;
    ISourceFrameProvider* active_source_provider = source_provider;
    if (active_source_provider == NULL) {
        active_source_provider = &default_source_provider;
    }
    Result<void> source_prepare_result =
        active_source_provider->Prepare(config.image_size_bytes);
    if (!source_prepare_result.IsOk()) {
        std::cerr << "master failed to prepare source frames: "
                  << source_prepare_result.GetMessage() << std::endl;
        (void)bus->UnregisterConsumerHandler("result_consumer");
        (void)http->UnregisterHandler(config.http_route);
        (void)host.Stop();
        (void)host.Fini();
        return 1;
    }
    config.source_buffer_mode = active_source_provider->SourceBufferMode();
    config.source_prepare_ms = active_source_provider->SourcePrepareMs();
    NotifyLog(observer, "info", "source frames prepared");

    {
        std::unique_lock<std::mutex> lock(shared_state->mutex);
        shared_state->run_start = SteadyClock::now();
        shared_state->last_result_activity = shared_state->run_start;
    }

    const std::chrono::microseconds period_us(
        static_cast<long long>(1000000.0 / static_cast<double>(config.publish_rate)));

    SteadyClock::time_point previous_source_time = shared_state->run_start;
    bool cancelled = false;
    for (int source_index = 0; source_index < config.task_count; ++source_index) {
        if (IsCancellationRequested(cancel_requested)) {
            cancelled = true;
            break;
        }
        const SteadyClock::time_point scheduled_time =
            shared_state->run_start + period_us * source_index;
        std::this_thread::sleep_until(scheduled_time);
        if (IsCancellationRequested(cancel_requested)) {
            cancelled = true;
            break;
        }

        const SteadyClock::time_point source_begin = SteadyClock::now();
        const std::string task_id = MakeTaskId(config.run_id, source_index);
        const std::string image_id = task_id;
        Result<MasterSourceFrame> source_frame =
            active_source_provider->GetFrame(source_index);
        if (!source_frame.IsOk() || !source_frame.Value().data) {
            {
                std::unique_lock<std::mutex> lock(shared_state->mutex);
                TaskState& state =
                    shared_state->tasks[static_cast<std::size_t>(source_index)];
                state.source_index = source_index;
                state.task_id = task_id;
                state.image_id = image_id;
                state.source_time = source_begin;
                state.source_offset_ms =
                    ElapsedMillis(source_begin, shared_state->run_start);
                state.source_interval_ms =
                    source_index == 0
                        ? 0.0
                        : ElapsedMillis(source_begin, previous_source_time);
                shared_state->image_lookup[image_id] = source_index;
            }
            FinalizeTask(
                shared_state.get(),
                observer,
                source_index,
                "source_failed",
                source_frame.IsOk()
                    ? "source frame data is empty"
                    : source_frame.GetMessage(),
                SteadyClock::now());
            previous_source_time = source_begin;
            continue;
        }
        std::shared_ptr<std::vector<char> > frame = source_frame.Value().data;
        const SteadyClock::time_point source_end = SteadyClock::now();

        TaskMessage task_message;
        task_message.kind = kTaskKindTask;
        task_message.task_id = task_id;
        task_message.run_id = config.run_id;
        task_message.image_id = image_id;
        task_message.source_index = source_index;
        task_message.image_bytes = frame->size();
        task_message.http_read_buffer_bytes = config.http_read_buffer_bytes;
        task_message.http_write_buffer_bytes = config.http_write_buffer_bytes;
        task_message.http_socket_receive_buffer_bytes =
            config.http_socket_receive_buffer_bytes;
        task_message.http_socket_send_buffer_bytes =
            config.http_socket_send_buffer_bytes;

        {
            std::unique_lock<std::mutex> lock(shared_state->mutex);
            TaskState& task = shared_state->tasks[static_cast<std::size_t>(source_index)];
            task.source_index = source_index;
            task.task_id = task_message.task_id;
            task.image_id = task_message.image_id;
            task.image_bytes = task_message.image_bytes;
            task.source_path = source_frame.Value().source_path;
            task.source_name = source_frame.Value().display_name;
            task.source_time = source_begin;
            task.source_offset_ms = ElapsedMillis(source_begin, shared_state->run_start);
            task.source_interval_ms =
                source_index == 0 ? 0.0 : ElapsedMillis(source_begin, previous_source_time);
            task.source_stage_ms = ElapsedMillis(source_end, source_begin);
            shared_state->image_lookup[task_message.image_id] = source_index;
        }
        previous_source_time = source_begin;

        TaskMessage message = task_message;
        const SteadyClock::time_point store_begin = SteadyClock::now();
        Result<std::string> token = image_store->Put(message.image_id, frame);
        const SteadyClock::time_point store_end = SteadyClock::now();
        if (token.IsOk()) {
            message.token = token.Value();
        }

        {
            std::unique_lock<std::mutex> lock(shared_state->mutex);
            TaskState& task =
                shared_state->tasks[static_cast<std::size_t>(source_index)];
            task.task_queue_ms = 0.0;
            task.image_store_put_ms = ElapsedMillis(store_end, store_begin);
            if (token.IsOk()) {
                task.image_store_status = "stored";
            } else {
                task.image_store_status = "put_failed";
                task.image_store_message = token.GetMessage();
            }
        }

        if (!token.IsOk()) {
            FinalizeTask(
                shared_state.get(),
                observer,
                source_index,
                "image_store_failed",
                token.GetMessage(),
                store_end);
            continue;
        }

        const SteadyClock::time_point enqueue_begin = SteadyClock::now();
        Result<void> enqueue_result = PublishTaskMessage(bus, message);
        const SteadyClock::time_point enqueue_end = SteadyClock::now();

        {
            std::unique_lock<std::mutex> lock(shared_state->mutex);
            TaskState& task =
                shared_state->tasks[static_cast<std::size_t>(source_index)];
            task.publish_ms = ElapsedMillis(enqueue_end, enqueue_begin);
            if (enqueue_result.IsOk()) {
                task.publish_ok = true;
                ++shared_state->sent_count;
            }
        }

        {
            MasterTaskSnapshot task_snapshot;
            MasterRunProgress progress_snapshot;
            {
                std::unique_lock<std::mutex> lock(shared_state->mutex);
                const TaskState& task =
                    shared_state->tasks[static_cast<std::size_t>(source_index)];
                task_snapshot = MakeTaskSnapshot(task);
                progress_snapshot = MakeProgressSnapshot(*shared_state);
            }
            NotifyTaskPublished(observer, task_snapshot);
            NotifyProgress(observer, progress_snapshot);
        }

        if (!enqueue_result.IsOk()) {
            const SteadyClock::time_point delete_begin = SteadyClock::now();
            Result<void> cleanup_result = image_store->Delete(message.image_id);
            const SteadyClock::time_point delete_end = SteadyClock::now();
            {
                std::unique_lock<std::mutex> lock(shared_state->mutex);
                TaskState& task =
                    shared_state->tasks[static_cast<std::size_t>(source_index)];
                task.image_store_delete_ms =
                    ElapsedMillis(delete_end, delete_begin);
                task.image_store_status =
                    cleanup_result.IsOk() ? "deleted" : "delete_failed";
                task.image_store_message = cleanup_result.GetMessage();
                if (!cleanup_result.IsOk()) {
                    ++shared_state->cleanup_failure_count;
                }
            }
            FinalizeTask(
                shared_state.get(),
                observer,
                source_index,
                "publish_enqueue_failed",
                enqueue_result.GetMessage(),
                delete_end);
            continue;
        }
    }

    bool timed_out = false;
    if (!cancelled) {
        std::unique_lock<std::mutex> lock(shared_state->mutex);
        if (config.timeout_ms == 0) {
            while (shared_state->finished_count < config.task_count) {
                if (IsCancellationRequested(cancel_requested)) {
                    cancelled = true;
                    break;
                }
                shared_state->completion_cv.wait_for(
                    lock,
                    std::chrono::milliseconds(200));
            }
        } else {
            const SteadyClock::time_point deadline =
                SteadyClock::now() + std::chrono::milliseconds(config.timeout_ms);
            while (shared_state->finished_count < config.task_count) {
                if (IsCancellationRequested(cancel_requested)) {
                    cancelled = true;
                    break;
                }
                const SteadyClock::time_point wake_time =
                    std::min<SteadyClock::time_point>(
                        deadline,
                        SteadyClock::now() + std::chrono::milliseconds(200));
                if (shared_state->completion_cv.wait_until(lock, wake_time) ==
                    std::cv_status::timeout) {
                    if (SteadyClock::now() >= deadline) {
                        timed_out = true;
                        break;
                    }
                }
            }
        }
    }

    if (timed_out || cancelled) {
        std::unique_lock<std::mutex> lock(shared_state->mutex);
        for (std::size_t index = 0; index < shared_state->tasks.size(); ++index) {
            if (!shared_state->tasks[index].finished) {
                shared_state->tasks[index].status =
                    cancelled ? "cancelled" : "timeout";
                shared_state->tasks[index].error_message =
                    cancelled ? "master cancelled" : "master timeout";
            }
        }
    }
    for (int index = 0; index < config.task_count; ++index) {
        bool should_cleanup = false;
        {
            std::unique_lock<std::mutex> lock(shared_state->mutex);
            should_cleanup = !shared_state->tasks[static_cast<std::size_t>(index)].finished;
        }
        if (should_cleanup) {
            CleanupTimedOutTask(shared_state, image_store, index);
            FinalizeTask(
                shared_state.get(),
                observer,
                index,
                cancelled ? "cancelled" : "timeout",
                cancelled ? "master cancelled" : "master timeout",
                SteadyClock::now());
        }
    }

    {
        std::unique_lock<std::mutex> lock(shared_state->mutex);
        shared_state->late_result_ack_only = true;
    }

    if (config.send_shutdown) {
        TaskMessage shutdown_message;
        shutdown_message.kind = kTaskKindShutdown;
        shutdown_message.task_id = MakeShutdownId(config.run_id);
        shutdown_message.run_id = config.run_id;
        shutdown_message.image_id = shutdown_message.task_id;
        shutdown_message.source_index = -1;
        shutdown_message.image_bytes = 0;
        Result<void> shutdown_publish = PublishControlMessage(bus, shutdown_message);
        if (!shutdown_publish.IsOk()) {
            std::cerr << "master failed to publish shutdown message: "
                      << shutdown_publish.GetMessage() << std::endl;
        }
    }

    (void)bus->UnregisterConsumerHandler("result_consumer");
    (void)http->UnregisterHandler(config.http_route);
    Result<void> stop_result = host.Stop();
    Result<void> fini_result = host.Fini();

    Result<void> metrics_result = WriteTaskMetrics(config, *shared_state);
    Result<void> summary_result = WriteSummary(config, *shared_state, *image_store);
    if (!metrics_result.IsOk()) {
        std::cerr << "master failed to write task metrics: "
                  << metrics_result.GetMessage() << std::endl;
    }
    if (!summary_result.IsOk()) {
        std::cerr << "master failed to write summary: "
                  << summary_result.GetMessage() << std::endl;
    }
    if (!stop_result.IsOk()) {
        std::cerr << "master stop failed: " << stop_result.GetMessage() << std::endl;
        return 1;
    }
    if (!fini_result.IsOk()) {
        std::cerr << "master fini failed: " << fini_result.GetMessage() << std::endl;
        return 1;
    }

    const bool success =
        !timed_out &&
        !cancelled &&
        shared_state->success_count == config.task_count &&
        shared_state->failure_count == 0 &&
        shared_state->cleanup_failure_count == 0 &&
        image_store->Size() == 0;

    MasterRunResult run_result;
    run_result.success = success;
    run_result.timed_out = timed_out;
    run_result.cancelled = cancelled;
    run_result.exit_code = success ? 0 : 1;
    run_result.sent_count = shared_state->sent_count;
    run_result.finished_count = shared_state->finished_count;
    run_result.success_count = shared_state->success_count;
    run_result.failure_count = shared_state->failure_count;
    run_result.cleanup_failure_count = shared_state->cleanup_failure_count;
    run_result.result_count = shared_state->result_count;
    run_result.image_store_remaining_count = image_store->Size();
    run_result.image_store_remaining_bytes = image_store->Bytes();
    if (observer != NULL) {
        observer->OnRunFinished(run_result);
    }
    return run_result.exit_code;
}

}  // namespace task_flow
}  // namespace examples
}  // namespace module_context
