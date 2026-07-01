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

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>

namespace xbox::cli {

class ProgressReporter {
public:
    ProgressReporter(std::size_t total, std::string_view unit = "files");
    ~ProgressReporter();

    // Increment the completed count by 1 (or by `delta`).
    void increment(std::size_t delta = 1) noexcept;

    // Set the current count directly (for callback-based progress).
    void set_current(std::size_t count) noexcept;

    // Set the current item label (for the spinner line).
    void set_current_label(std::string_view label);

    // Mark as complete (force 100% display).
    void finish() noexcept;

    // Disable output (e.g. when --quiet)
    void set_enabled(bool e) noexcept { enabled_ = e; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }

private:
    void render() noexcept;
    void render_tty();
    void render_non_tty();

    std::atomic<std::size_t> current_{0};
    std::size_t              total_{0};
    std::string              unit_;
    std::string              current_label_{"Working..."};
    std::mutex               mutex_;
    bool                     enabled_{true};
    bool                     finished_{false};
    bool                     is_tty_{false};
    int                      tty_width_{80};
    std::size_t              last_printed_{static_cast<std::size_t>(-1)};
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace xbox::cli
