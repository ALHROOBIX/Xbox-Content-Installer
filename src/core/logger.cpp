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
#include "xbox/core/logger.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>

#if defined(_WIN32)
#  include <io.h>
#  include <windows.h>
#  define ISATTY _isatty
#  define FILeno _fileno
#else
#  include <unistd.h>
#  define ISATTY isatty
#  define FILeno fileno
#endif

namespace xbox::log {

namespace {
constexpr std::string_view level_color(Level l) noexcept {
    switch (l) {
        case Level::Trace: return "\x1b[90m";  // bright black (gray)
        case Level::Debug: return "\x1b[36m";  // cyan
        case Level::Info:  return "\x1b[32m";  // green
        case Level::Warn:  return "\x1b[33m";  // yellow
        case Level::Error: return "\x1b[31m";  // red
        case Level::Off:   return "";
    }
    return "";
}

constexpr std::string_view level_label(Level l) noexcept {
    switch (l) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
        case Level::Off:   return "OFF  ";
    }
    return "     ";
}

constexpr std::string_view COLOR_RESET = "\x1b[0m";
} // namespace

Level parse_level(std::string_view s) noexcept {
    if (s == "trace" || s == "TRACE") return Level::Trace;
    if (s == "debug" || s == "DEBUG") return Level::Debug;
    if (s == "info"  || s == "INFO")  return Level::Info;
    if (s == "warn"  || s == "WARN")  return Level::Warn;
    if (s == "error" || s == "ERROR") return Level::Error;
    if (s == "off"   || s == "OFF")   return Level::Off;
    return Level::Info;
}

std::string_view level_name(Level l) noexcept {
    return level_label(l);
}

Logger::Logger() {
    // Auto-enable color if stderr is a TTY
    color_enabled_ = (ISATTY(FILeno(stderr)) != 0);

    // Honor XBOX_LOG_LEVEL env var
    if (const char* env = std::getenv("XBOX_LOG_LEVEL")) {
        level_.store(parse_level(env), std::memory_order_relaxed);
    }

    // Honor XBOX_LOG_COLOR env var (force on/off)
    if (const char* env = std::getenv("XBOX_LOG_COLOR")) {
        if (std::strcmp(env, "1") == 0 || std::strcmp(env, "on") == 0)  color_enabled_ = true;
        if (std::strcmp(env, "0") == 0 || std::strcmp(env, "off") == 0) color_enabled_ = false;
    }

    if (const char* env = std::getenv("XBOX_LOG_TIMESTAMP")) {
        show_timestamp_ = (std::strcmp(env, "1") == 0 || std::strcmp(env, "on") == 0);
    }
    if (const char* env = std::getenv("XBOX_LOG_THREAD_ID")) {
        show_thread_id_ = (std::strcmp(env, "1") == 0 || std::strcmp(env, "on") == 0);
    }
}

void Logger::log(Level l, std::string_view msg, const std::source_location& /*loc*/) {
    if (level_.load(std::memory_order_relaxed) > l) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Build the line in a small buffer to minimize fprintf calls
    char header[128];
    std::size_t pos = 0;

    if (show_timestamp_) {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        pos += std::snprintf(header + pos, sizeof(header) - pos,
            "%04d-%02d-%02d %02d:%02d:%02d.%03lld ",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            static_cast<long long>(ms.count()));
    }

    if (show_thread_id_) {
        auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
        pos += std::snprintf(header + pos, sizeof(header) - pos,
            "[tid=%016llx] ", static_cast<unsigned long long>(tid));
    }

    if (color_enabled_) {
        // Format: "header color label reset message"
        // All string_view args use %.*s with explicit length to avoid
        // relying on null-termination (string_view::data() is NOT guaranteed
        // to be null-terminated).
        auto color = level_color(l);
        auto label = level_label(l);
        auto reset = COLOR_RESET;
        std::fprintf(stderr, "%s%.*s%.*s%.*s %.*s\n",
            header,
            static_cast<int>(color.size()), color.data(),
            static_cast<int>(label.size()), label.data(),
            static_cast<int>(reset.size()), reset.data(),
            static_cast<int>(msg.size()), msg.data());
    } else {
        auto label = level_label(l);
        std::fprintf(stderr, "%s%.*s %.*s\n",
            header,
            static_cast<int>(label.size()), label.data(),
            static_cast<int>(msg.size()), msg.data());
    }
}

} // namespace xbox::log
