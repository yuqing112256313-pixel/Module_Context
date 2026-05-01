#include "foundation/concurrent/ThreadPool.h"

namespace foundation {
namespace concurrent {

namespace {

foundation::base::Result<void> MakeOkResult() {
    return foundation::base::Result<void>();
}

foundation::base::Result<void> MakeErrorResult(
    foundation::base::ErrorCode error_code,
    const char* message) {
    return foundation::base::Result<void>(error_code, message);
}

}  // namespace

ThreadPool::ThreadPool(std::size_t thread_count)
    : thread_count_(thread_count)
    , mutex_()
    , workers_()
    , queue_()
    , state_(kCreated)
    , active_tasks_(0) 
{
}

ThreadPool::~ThreadPool() {
    try {
        (void)Shutdown();
    } catch (...) {
        // Destructor must not throw.
    }
}

foundation::base::Result<void> ThreadPool::Start() {
    std::vector<std::thread> local_workers;
    std::unique_ptr<ThreadSafeQueue<Task> > local_queue(
        new ThreadSafeQueue<Task>());
    bool start_failed = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (thread_count_ == 0) {
            return MakeErrorResult(
                foundation::base::ErrorCode::kInvalidState,
                "ThreadPool thread_count must be greater than 0");
        }

        if (GetState() != kCreated) {
            return MakeErrorResult(
                foundation::base::ErrorCode::kInvalidState,
                "ThreadPool can only be started from the created state");
        }

        try {
            local_workers.reserve(thread_count_);
            for (std::size_t i = 0; i < thread_count_; ++i) {
                local_workers.push_back(
                    std::thread(&ThreadPool::WorkerLoop, this, local_queue.get()));
            }
        } catch (...) {
            local_queue->CloseAndClear();
            start_failed = true;
        }

        if (!start_failed) {
            workers_.swap(local_workers);
            queue_.swap(local_queue);
            SetState(kRunning);
            return MakeOkResult();
        }
    }

    JoinWorkers(&local_workers);

    return MakeErrorResult(
        foundation::base::ErrorCode::kInvalidState,
        "Failed to create worker threads");
}

foundation::base::Result<void> ThreadPool::Shutdown() {
    return ShutdownInternal(false);
}

foundation::base::Result<void> ThreadPool::ShutdownNow() {
    return ShutdownInternal(true);
}

bool ThreadPool::IsRunning() const {
    return GetState() == kRunning;
}

bool ThreadPool::IsStopping() const {
    State state = GetState();
    return state == kStoppingGraceful || state == kStoppingImmediate;
}

bool ThreadPool::IsStopped() const {
    return GetState() == kStopped;
}

std::size_t ThreadPool::GetThreadCount() const {
    return thread_count_;
}

std::size_t ThreadPool::GetActiveTaskCount() const {
    return active_tasks_.load(std::memory_order_acquire);
}

std::size_t ThreadPool::GetPendingTaskCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!queue_) {
        return 0;
    }
    return queue_->Size();
}

foundation::base::Result<void> ThreadPool::ShutdownInternal(bool immediate) {
    std::vector<std::thread> local_workers;
    std::unique_ptr<ThreadSafeQueue<Task> > local_queue;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        State state = GetState();

        if (state == kStopped) {
            return MakeOkResult();
        }

        if (state == kCreated) {
            SetState(kStopped);
            return MakeOkResult();
        }

        if ((state == kStoppingGraceful || state == kStoppingImmediate) &&
            !queue_ && workers_.empty()) {
            return MakeOkResult();
        }

        if (state == kRunning) {
            SetState(immediate ? kStoppingImmediate : kStoppingGraceful);
        } else if (state == kStoppingGraceful) {
            if (immediate) {
                SetState(kStoppingImmediate);
            }
        }

        workers_.swap(local_workers);
        local_queue = std::move(queue_);
    }

    if (local_queue) {
        if (immediate) {
            local_queue->CloseAndClear();
        } else {
            local_queue->Close();
        }
    }

    JoinWorkers(&local_workers);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        SetState(kStopped);
    }

    return MakeOkResult();
}

ThreadPool::State ThreadPool::GetState() const {
    return state_.load(std::memory_order_acquire);
}

void ThreadPool::SetState(State state) {
    state_.store(state, std::memory_order_release);
}

bool ThreadPool::CanAcceptTasksLocked() const {
    return state_.load(std::memory_order_relaxed) == kRunning &&
           queue_.get() != NULL &&
           !queue_->IsClosed();
}

void ThreadPool::JoinWorkers(std::vector<std::thread>* workers) {
    if (workers == NULL) {
        return;
    }

    for (std::size_t i = 0; i < workers->size(); ++i) {
        if ((*workers)[i].joinable()) {
            (*workers)[i].join();
        }
    }

    workers->clear();
}

void ThreadPool::WorkerLoop(ThreadSafeQueue<Task>* queue) {
    if (queue == NULL) {
        return;
    }

    while (true) {
        foundation::base::Result<Task> pop_result = queue->Pop();
        if (!pop_result.IsOk()) {
            return;
        }

        Task task = std::move(pop_result.Value());
        ActiveTaskGuard guard(&active_tasks_);

        try {
            task();
        } catch (...) {
            // Worker thread must not terminate due to an escaped exception.
            // For std::packaged_task-based tasks, exceptions are stored in future.
        }
    }
}

ThreadPool::ActiveTaskGuard::ActiveTaskGuard(
    std::atomic<std::size_t>* counter)
    : counter_(counter) {
    if (counter_ != NULL) {
        counter_->fetch_add(1, std::memory_order_acq_rel);
    }
}

ThreadPool::ActiveTaskGuard::~ActiveTaskGuard() {
    if (counter_ != NULL) {
        counter_->fetch_sub(1, std::memory_order_acq_rel);
    }
}

}  // namespace concurrent
}  // namespace foundation