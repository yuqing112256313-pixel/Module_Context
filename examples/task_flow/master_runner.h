#pragma once

#include "foundation/base/Result.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace module_context {
namespace examples {
namespace task_flow {

struct MasterRunConfig {
    MasterRunConfig();

    std::string module_config_path;
    std::string run_id;
    std::string report_dir;
    std::string profile_name;
    std::string http_route;
    int task_count;
    int publish_rate;
    std::size_t image_size_bytes;
    std::size_t image_store_capacity_bytes;
    int image_store_ttl_ms;
    int master_write_publish_threads;
    int master_result_threads;
    int worker_count;
    int timeout_ms;
    std::size_t http_chunk_bytes;
    std::size_t http_read_buffer_bytes;
    std::size_t http_write_buffer_bytes;
    std::size_t http_socket_receive_buffer_bytes;
    std::size_t http_socket_send_buffer_bytes;
    bool send_shutdown;
    std::string source_buffer_mode;
    double source_prepare_ms;
};

struct MasterSourceFrame {
    MasterSourceFrame();

    std::shared_ptr<std::vector<char> > data;
    std::string source_path;
    std::string display_name;
};

class ISourceFrameProvider {
public:
    virtual ~ISourceFrameProvider();

    virtual foundation::base::Result<void> Prepare(
        std::size_t fallback_image_size_bytes) = 0;
    virtual foundation::base::Result<MasterSourceFrame> GetFrame(
        int source_index) = 0;
    virtual std::string SourceBufferMode() const = 0;
    virtual double SourcePrepareMs() const = 0;
};

class PatternFrameProvider : public ISourceFrameProvider {
public:
    PatternFrameProvider();

    foundation::base::Result<void> Prepare(
        std::size_t fallback_image_size_bytes) override;
    foundation::base::Result<MasterSourceFrame> GetFrame(
        int source_index) override;
    std::string SourceBufferMode() const override;
    double SourcePrepareMs() const override;

private:
    std::shared_ptr<std::vector<char> > frame_;
    double source_prepare_ms_;
};

struct MasterTaskSnapshot {
    MasterTaskSnapshot();

    int source_index;
    std::string task_id;
    std::string image_id;
    std::string worker_id;
    std::string status;
    std::string detail_message;
    std::string image_store_status;
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
};

struct MasterRunProgress {
    MasterRunProgress();

    int sent_count;
    int finished_count;
    int success_count;
    int failure_count;
    int cleanup_failure_count;
    int result_count;
    int task_count;
    double elapsed_ms;
};

struct MasterRunResult {
    MasterRunResult();

    bool success;
    bool timed_out;
    bool cancelled;
    int exit_code;
    int sent_count;
    int finished_count;
    int success_count;
    int failure_count;
    int cleanup_failure_count;
    int result_count;
    std::size_t image_store_remaining_count;
    std::size_t image_store_remaining_bytes;
};

class IMasterRunObserver {
public:
    virtual ~IMasterRunObserver();

    virtual void OnLog(
        const std::string& level,
        const std::string& message);
    virtual void OnTaskPublished(const MasterTaskSnapshot& task);
    virtual void OnTaskUpdated(const MasterTaskSnapshot& task);
    virtual void OnProgress(const MasterRunProgress& progress);
    virtual void OnRunFinished(const MasterRunResult& result);
};

foundation::base::Result<MasterRunConfig> ParseMasterRunConfigFromArgs(
    const std::map<std::string, std::string>& args);

int RunTaskFlowMaster(
    const MasterRunConfig& config,
    ISourceFrameProvider* source_provider,
    IMasterRunObserver* observer,
    std::atomic_bool* cancel_requested);

}  // namespace task_flow
}  // namespace examples
}  // namespace module_context
