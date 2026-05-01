#ifndef FOUNDATION_CONCURRENT_THREADPOOL_H_
#define FOUNDATION_CONCURRENT_THREADPOOL_H_

#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "foundation/base/ErrorCode.h"
#include "foundation/base/Export.h"
#include "foundation/base/NonCopyable.h"
#include "foundation/base/Result.h"
#include "foundation/concurrent/ThreadSafeQueue.h"

namespace foundation {
namespace concurrent {

class FOUNDATION_API ThreadPool : private foundation::base::NonCopyable {
public:
    typedef std::function<void()> Task;

    explicit ThreadPool(std::size_t thread_count);
    ~ThreadPool();

public:
    foundation::base::Result<void> Start();
    foundation::base::Result<void> Shutdown();
    foundation::base::Result<void> ShutdownNow();

public:
    template <typename F, typename... Args>
    auto Submit(F&& f, Args&&... args)
        -> foundation::base::Result<
            std::future<typename std::result_of<F(Args...)>::type> >;

public:
    bool IsRunning() const;
    bool IsStopping() const;
    bool IsStopped() const;

    std::size_t GetThreadCount() const;
    std::size_t GetActiveTaskCount() const;
    std::size_t GetPendingTaskCount() const;

private:
    enum State {
        kCreated = 0,
        kRunning = 1,
        kStoppingGraceful = 2,
        kStoppingImmediate = 3,
        kStopped = 4
    };

private:
    foundation::base::Result<void> ShutdownInternal(bool immediate);

    State GetState() const;
    void SetState(State state);
    bool CanAcceptTasksLocked() const;

    void JoinWorkers(std::vector<std::thread>* workers);
    void WorkerLoop(ThreadSafeQueue<Task>* queue);

private:
    class ActiveTaskGuard : private foundation::base::NonCopyable {
    public:
        explicit ActiveTaskGuard(std::atomic<std::size_t>* counter);
        ~ActiveTaskGuard();

    private:
        std::atomic<std::size_t>* counter_;
    };

private:
    const std::size_t thread_count_;

    mutable std::mutex mutex_;
    std::vector<std::thread> workers_;
    std::unique_ptr<ThreadSafeQueue<Task> > queue_;

    std::atomic<State> state_;
    std::atomic<std::size_t> active_tasks_;
};

// ============================================================================
// Template implementation
// ============================================================================

template <typename F, typename... Args>
auto ThreadPool::Submit(F&& f, Args&&... args)
    -> foundation::base::Result<
        std::future<typename std::result_of<F(Args...)>::type> > {
    typedef typename std::result_of<F(Args...)>::type ReturnType;
    typedef std::future<ReturnType> FutureType;
    typedef foundation::base::Result<FutureType> SubmitResult;

    std::shared_ptr<std::packaged_task<ReturnType()> > task(
        new std::packaged_task<ReturnType()>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)));

    FutureType future = task->get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!CanAcceptTasksLocked()) {
            return SubmitResult(
                foundation::base::ErrorCode::kInvalidState,
                "ThreadPool is not running or is stopping");
        }

        foundation::base::Result<void> push_result =
            queue_->Push(Task([task]() { (*task)(); }));

        if (!push_result.IsOk()) {
            return SubmitResult(
                foundation::base::ErrorCode::kInvalidState,
                "ThreadPool is not accepting new tasks");
        }
    }

    return SubmitResult(std::move(future));
}

}  // namespace concurrent
}  // namespace foundation

#endif  // FOUNDATION_CONCURRENT_THREADPOOL_H_
