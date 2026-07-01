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

#include "xbox/core/types.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <source_location>
#include <string_view>
#include <thread>

namespace xbox::log {

enum class Level : u8 {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Off = 0xFF,
};

// Convert string ("trace", "debug", "info", "warn", "error", "off") to Level.
[[nodiscard]] Level parse_level(std::string_view s) noexcept;
[[nodiscard]] std::string_view level_name(Level l) noexcept;

class Logger {
public:
    // Singleton access - logger is process-wide.
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void set_level(Level l) noexcept { level_.store(l, std::memory_order_relaxed); }
    [[nodiscard]] Level level() const noexcept { return level_.load(std::memory_order_relaxed); }

    void set_color_enabled(bool enabled) noexcept { color_enabled_ = enabled; }
    [[nodiscard]] bool color_enabled() const noexcept { return color_enabled_; }

    void set_show_timestamp(bool enabled) noexcept { show_timestamp_ = enabled; }
    void set_show_thread_id(bool enabled) noexcept { show_thread_id_ = enabled; }

    // Core logging function - thread-safe.
    void log(Level l, std::string_view msg,
             const std::source_location& loc = std::source_location::current());

    // Convenience fluent builders (prefer the macros below)
    template <typename... Args>
    void trace(std::format_string<Args...> fmt, Args&&... args) {
        if (level() <= Level::Trace) log(Level::Trace, std::format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) {
        if (level() <= Level::Debug) log(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) {
        if (level() <= Level::Info) log(Level::Info, std::format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) {
        if (level() <= Level::Warn) log(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) {
        if (level() <= Level::Error) log(Level::Error, std::format(fmt, std::forward<Args>(args)...));
    }

private:
    Logger();
    std::atomic<Level> level_{Level::Info};
    bool color_enabled_{false};
    bool show_timestamp_{true};
    bool show_thread_id_{false};
    std::mutex mutex_{};
};

// Global accessor
[[nodiscard]] inline Logger& logger() { return Logger::instance(); }

} // namespace xbox::log

// Convenience macros (compile-time enabled when level threshold met)
#define XBOX_LOG_TRACE(msg, ...) ::xbox::log::logger().trace(msg, ##__VA_ARGS__)
#define XBOX_LOG_DEBUG(msg, ...) ::xbox::log::logger().debug(msg, ##__VA_ARGS__)
#define XBOX_LOG_INFO(msg, ...)  ::xbox::log::logger().info(msg,  ##__VA_ARGS__)
#define XBOX_LOG_WARN(msg, ...)  ::xbox::log::logger().warn(msg,  ##__VA_ARGS__)
#define XBOX_LOG_ERROR(msg, ...) ::xbox::log::logger().error(msg, ##__VA_ARGS__)
