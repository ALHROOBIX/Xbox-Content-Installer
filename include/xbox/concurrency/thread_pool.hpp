// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 ALHROOBIX
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "xbox/core/errors.hpp"
#include "xbox/core/result.hpp"
#include "xbox/core/types.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace xbox::concurrency {

// What should happen when submitting to a full queue?
enum class QueueFullPolicy : u8 {
    Block,    // Wait until a slot is available
    Reject,   // Return error immediately
};

class ThreadPool {
public:
    // Construct with `num_threads` workers. If 0, uses hardware_concurrency.
    // `max_queue_size` of 0 means unbounded.
    explicit ThreadPool(std::size_t num_threads = 0,
                        std::size_t max_queue_size = 4096);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Submit a callable. Returns a future for the result.
    // If queue is full and policy is Reject, returns a failed future.
    template <typename F, typename... Args>
    [[nodiscard]] auto submit(QueueFullPolicy policy, F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    // Convenience: submit with default policy (Block)
    template <typename F, typename... Args>
    [[nodiscard]] auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        return submit(QueueFullPolicy::Block, std::forward<F>(f), std::forward<Args>(args)...);
    }

    // Submit a void-returning callable that returns Result<void, Error>.
    // Wraps it so the future is a Result future.
    template <typename F>
    [[nodiscard]] std::future<Result<void, Error>> submit_result(F&& f) {
        return submit(QueueFullPolicy::Block, [f = std::forward<F>(f)]() mutable -> Result<void, Error> {
            return f();
        });
    }

    // Block until all currently-queued tasks finish.
    // New tasks may still be submitted during wait().
    void wait_for_tasks() const;

    // Shutdown the pool (signal stop, wait for workers). Idempotent.
    void shutdown() noexcept;

    // Pool metrics
    [[nodiscard]] std::size_t worker_count() const noexcept { return workers_.size(); }
    [[nodiscard]] std::size_t queued_task_count() const;
    [[nodiscard]] bool        is_stopped() const noexcept { return stop_.load(std::memory_order_relaxed); }

private:
    void worker_loop();

    mutable std::mutex      queue_mutex_{};
    std::condition_variable queue_not_empty_{};
    std::condition_variable queue_not_full_{};
    std::condition_variable task_done_{};

    std::deque<std::function<void()>> tasks_{};
    const std::size_t      max_queue_size_{4096};
    std::atomic<bool>      stop_{false};
    std::atomic<std::size_t> active_tasks_{0};

    std::vector<std::thread> workers_{};
};

// ---------------------------------------------------------------------------
// Template implementation
// ---------------------------------------------------------------------------
template <typename F, typename... Args>
auto ThreadPool::submit(QueueFullPolicy policy, F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {
    using R = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<R()>>(
        [f = std::forward<F>(f),
         args_tuple = std::make_tuple(std::forward<Args>(args)...)]() mutable {
            return std::apply(std::move(f), std::move(args_tuple));
        });

    auto fut = task->get_future();

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        if (stop_.load(std::memory_order_relaxed)) {
            throw std::runtime_error("ThreadPool: submit() after shutdown");
        }

        // Wait for queue space if blocking policy
        if (max_queue_size_ > 0) {
            if (policy == QueueFullPolicy::Block) {
                queue_not_full_.wait(lock, [this] {
                    return tasks_.size() < max_queue_size_ ||
                           stop_.load(std::memory_order_relaxed);
                });
                if (stop_.load(std::memory_order_relaxed)) {
                    throw std::runtime_error("ThreadPool: shutdown during submit");
                }
            } else {
                // Reject policy
                if (tasks_.size() >= max_queue_size_) {
                    throw std::runtime_error("ThreadPool: queue full (Reject policy)");
                }
            }
        }

        tasks_.emplace_back([task = std::move(task)]() mutable {
            (*task)();
        });
    }

    queue_not_empty_.notify_one();
    return fut;
}

} // namespace xbox::concurrency
