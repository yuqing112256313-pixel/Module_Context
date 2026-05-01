#ifndef FOUNDATION_CONCURRENT_THREADSAFEQUEUE_H_
#define FOUNDATION_CONCURRENT_THREADSAFEQUEUE_H_

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

#include "foundation/base/ErrorCode.h"
#include "foundation/base/NonCopyable.h"
#include "foundation/base/Result.h"

namespace foundation {
namespace concurrent {

// ============================================================================
// ThreadSafeQueue<T>
//
// A thread-safe FIFO queue for producer-consumer scenarios.
//
// Semantics
// ---------
// 1. Push()
//    - Enqueues an item when the queue is open.
//    - Returns kQueueClosed if the queue has already been closed.
//
// 2. TryPop()
//    - Non-blocking pop.
//    - Returns:
//        * success            : one item was popped
//        * kQueueEmpty        : queue is open but currently empty
//        * kQueueClosed       : queue is closed and empty; no more items will
//                               ever arrive
//
// 3. Pop()
//    - Blocking pop.
//    - Waits until:
//        * an item becomes available, or
//        * the queue becomes closed
//    - Returns:
//        * success            : one item was popped
//        * kQueueClosed       : queue is closed and empty
//
// 4. Close()
//    - Marks the queue closed and wakes all blocked consumers.
//    - Remaining queued items are preserved and can still be drained.
//
// 5. CloseAndClear()
//    - Marks the queue closed, discards all queued items, and wakes all blocked
//      consumers.
//    - After this call, Pop()/TryPop() return kQueueClosed.
//
// Thread-safety
// -------------
// - All public methods are thread-safe.
// - The destructor calls Close() to wake blocked waiters, but object lifetime
//   is still the caller's responsibility.
// - The queue object must not be destroyed while other threads may still call
//   any member function on it. The owner must stop/join worker threads before
//   destroying the queue.
//
// Notes
// -----
// - This queue is unbounded.
// - T must be copy-constructible for Push(const T&) and move-constructible for
//   Push(T&&)/Pop()/TryPop().
// ============================================================================
template <typename T>
class ThreadSafeQueue : private foundation::base::NonCopyable {
public:
    ThreadSafeQueue()
        : closed_(false) {
    }

    ~ThreadSafeQueue() {
        Close();
    }

public:
    // ------------------------------------------------------------------------
    // Push (copy)
    //
    // Returns:
    //   - success      : item was enqueued
    //   - kQueueClosed : queue has already been closed
    // ------------------------------------------------------------------------
    foundation::base::Result<void> Push(const T& value) {
        T copy(value);
        return PushInternal(std::move(copy));
    }

    // ------------------------------------------------------------------------
    // Push (move)
    //
    // Returns:
    //   - success      : item was enqueued
    //   - kQueueClosed : queue has already been closed
    // ------------------------------------------------------------------------
    foundation::base::Result<void> Push(T&& value) {
        return PushInternal(std::move(value));
    }

    // ------------------------------------------------------------------------
    // TryPop
    //
    // Non-blocking pop.
    //
    // Returns:
    //   - success      : item was popped
    //   - kQueueEmpty  : queue is open but empty at the moment
    //   - kQueueClosed : queue is closed and empty
    // ------------------------------------------------------------------------
    foundation::base::Result<T> TryPop() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (queue_.empty()) {
            if (closed_) {
                return foundation::base::Result<T>(
                    foundation::base::ErrorCode::kQueueClosed,
                    "ThreadSafeQueue::TryPop failed: queue is closed and empty");
            }

            return foundation::base::Result<T>(
                foundation::base::ErrorCode::kQueueEmpty,
                "ThreadSafeQueue::TryPop failed: queue is empty");
        }

        T value(std::move(queue_.front()));
        queue_.pop_front();
        return foundation::base::Result<T>(std::move(value));
    }

    // ------------------------------------------------------------------------
    // Pop
    //
    // Blocking pop.
    //
    // Waits until:
    //   - an item becomes available, or
    //   - the queue is closed
    //
    // Returns:
    //   - success      : item was popped
    //   - kQueueClosed : queue is closed and empty
    // ------------------------------------------------------------------------
    foundation::base::Result<T> Pop() {
        std::unique_lock<std::mutex> lock(mutex_);

        not_empty_cv_.wait(lock, [this]() {
            return closed_ || !queue_.empty();
        });

        if (queue_.empty()) {
            return foundation::base::Result<T>(
                foundation::base::ErrorCode::kQueueClosed,
                "ThreadSafeQueue::Pop failed: queue is closed and empty");
        }

        T value(std::move(queue_.front()));
        queue_.pop_front();
        return foundation::base::Result<T>(std::move(value));
    }

    // ------------------------------------------------------------------------
    // Close
    //
    // Closes the queue and wakes all blocked waiters.
    // Existing queued items remain available for popping.
    //
    // Idempotent.
    // ------------------------------------------------------------------------
    void Close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return;
            }
            closed_ = true;
        }

        not_empty_cv_.notify_all();
    }

    // ------------------------------------------------------------------------
    // CloseAndClear
    //
    // Atomically closes the queue, discards all pending items, and wakes all
    // blocked waiters.
    //
    // After this call:
    //   - Push() fails with kQueueClosed
    //   - Pop()/TryPop() return kQueueClosed
    //
    // Idempotent.
    // ------------------------------------------------------------------------
    void CloseAndClear() {
        std::deque<T> garbage;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!queue_.empty()) {
                garbage.swap(queue_);
            }
            closed_ = true;
        }

        not_empty_cv_.notify_all();
    }

public:
    // ------------------------------------------------------------------------
    // Observers
    //
    // These methods provide a snapshot under lock.
    // Their results may become stale immediately after return in concurrent use.
    // ------------------------------------------------------------------------
    bool IsClosed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    std::size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    foundation::base::Result<void> PushInternal(T&& value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) {
                return foundation::base::Result<void>(
                    foundation::base::ErrorCode::kQueueClosed,
                    "ThreadSafeQueue::Push failed: queue is closed");
            }

            queue_.push_back(std::move(value));
        }

        not_empty_cv_.notify_one();
        return foundation::base::Result<void>();
    }

    mutable std::mutex mutex_;
    std::condition_variable not_empty_cv_;
    std::deque<T> queue_;
    bool closed_;
};

}  // namespace concurrent
}  // namespace foundation

#endif  // FOUNDATION_CONCURRENT_THREADSAFEQUEUE_H_