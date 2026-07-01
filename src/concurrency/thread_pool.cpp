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
#include "xbox/concurrency/thread_pool.hpp"

#include "xbox/core/errors.hpp"
#include "xbox/core/logger.hpp"

#include <algorithm>
#include <exception>
#include <thread>

namespace xbox::concurrency {

ThreadPool::ThreadPool(std::size_t num_threads, std::size_t max_queue_size)
    : max_queue_size_(max_queue_size) {
    if (num_threads == 0) {
        num_threads = std::max<std::size_t>(1u, std::thread::hardware_concurrency());
    }
    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::worker_loop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_not_empty_.wait(lock, [this] {
                return !tasks_.empty() || stop_.load(std::memory_order_relaxed);
            });

            if (stop_.load(std::memory_order_relaxed) && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop_front();
            active_tasks_.fetch_add(1, std::memory_order_relaxed);
            queue_not_full_.notify_one();
        }

        try {
            task();
        } catch (const std::exception& e) {
            XBOX_LOG_ERROR("ThreadPool task threw exception: {}", e.what());
        } catch (...) {
            XBOX_LOG_ERROR("ThreadPool task threw unknown exception");
        }

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            active_tasks_.fetch_sub(1, std::memory_order_relaxed);
            if (tasks_.empty() && active_tasks_.load(std::memory_order_relaxed) == 0) {
                task_done_.notify_all();
            }
        }
    }
}

void ThreadPool::wait_for_tasks() const {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    const_cast<std::condition_variable&>(task_done_).wait(lock, [this] {
        return tasks_.empty() && active_tasks_.load(std::memory_order_relaxed) == 0;
    });
}

void ThreadPool::shutdown() noexcept {
    if (stop_.exchange(true, std::memory_order_acq_rel)) {
        return;  // already stopped
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_not_empty_.notify_all();
        queue_not_full_.notify_all();
    }
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();
}

std::size_t ThreadPool::queued_task_count() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

} // namespace xbox::concurrency
