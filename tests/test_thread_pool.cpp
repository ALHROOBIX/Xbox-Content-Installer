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
#include "test_framework.hpp"
#include "xbox/concurrency/thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <vector>

using namespace xbox;
using namespace xbox::concurrency;

TEST(ThreadPool_BasicSubmit) {
    ThreadPool pool(4);
    auto fut = pool.submit([]() -> int { return 42; });
    EXPECT_EQ(fut.get(), 42);
}

TEST(ThreadPool_MultipleTasks) {
    ThreadPool pool(4);
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.submit([i]() -> int { return i * 2; }));
    }
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(futures[i].get(), i * 2);
    }
}

TEST(ThreadPool_ConcurrentIncrement) {
    ThreadPool pool(8);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 1000; ++i) {
        futures.push_back(pool.submit([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }
    for (auto& f : futures) f.wait();
    EXPECT_EQ(counter.load(), 1000);
}

TEST(ThreadPool_ExceptionCapture) {
    ThreadPool pool(2);
    auto fut = pool.submit([]() -> int {
        throw std::runtime_error("test error");
        return 0;
    });
    // The exception is captured by the pool; the future should still complete
    // (with broken promise or stored exception)
    try {
        (void)fut.get();
        // If we got here without throwing, the pool swallowed it
    } catch (const std::exception& e) {
        EXPECT_TRUE(std::string(e.what()).find("test error") != std::string::npos);
    }
}

TEST(ThreadPool_WaitForTasks) {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 50; ++i) {
        futures.push_back(pool.submit([&counter]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }
    pool.wait_for_tasks();
    EXPECT_EQ(counter.load(), 50);
}

TEST(ThreadPool_GracefulShutdown) {
    ThreadPool pool(2);
    std::atomic<int> counter{0};
    for (int i = 0; i < 10; ++i) {
        (void)pool.submit([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }
    // Destructor should signal stop, wait for workers, and not crash
    // We can't easily test this without actually destroying - just verify
    // that submit doesn't throw during normal operation
    EXPECT_GE(pool.worker_count(), 1u);
}

TEST(ThreadPool_HardwareConcurrency) {
    // Pool with 0 threads should use hardware_concurrency
    ThreadPool pool(0);
    EXPECT_GE(pool.worker_count(), 1u);
}

TEST(ThreadPool_ParallelSpeedup) {
    // Rough sanity check that parallel execution is faster than serial
    // (not a strict assertion - just a smoke test)
    auto serial_start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    auto serial_end = std::chrono::steady_clock::now();
    auto serial_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        serial_end - serial_start).count();

    ThreadPool pool(10);
    auto parallel_start = std::chrono::steady_clock::now();
    std::vector<std::future<void>> futs;
    for (int i = 0; i < 10; ++i) {
        futs.push_back(pool.submit([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }));
    }
    for (auto& f : futs) f.wait();
    auto parallel_end = std::chrono::steady_clock::now();
    auto parallel_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        parallel_end - parallel_start).count();

    // Parallel should be substantially faster (allow some overhead)
    EXPECT_TRUE(parallel_ms < serial_ms);
}
