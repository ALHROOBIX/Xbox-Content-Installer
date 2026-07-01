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
#include "xbox/cli/progress.hpp"

#include "xbox/utils/string_utils.hpp"

#include <cstdio>
#include <iostream>

#if defined(_WIN32)
#  include <io.h>
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/ioctl.h>
#endif

namespace xbox::cli {

ProgressReporter::ProgressReporter(std::size_t total, std::string_view unit)
    : total_(total), unit_(unit), start_time_(std::chrono::steady_clock::now()) {
#if defined(_WIN32)
    is_tty_ = (_isatty(_fileno(stderr)) != 0);
#else
    is_tty_ = (isatty(fileno(stderr)) != 0);
#endif
    tty_width_ = str::terminal_width();
    if (tty_width_ < 40) tty_width_ = 80;
    if (!is_tty_) enabled_ = false;  // disable animation for non-TTY
}

ProgressReporter::~ProgressReporter() {
    if (enabled_ && !finished_) {
        finish();
    }
}

void ProgressReporter::increment(std::size_t delta) noexcept {
    current_.fetch_add(delta, std::memory_order_relaxed);
    if (enabled_) render();
}

void ProgressReporter::set_current(std::size_t count) noexcept {
    current_.store(count, std::memory_order_relaxed);
    if (enabled_) render();
}

void ProgressReporter::set_current_label(std::string_view label) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_label_ = std::string(label);
    // Render is called INSIDE the lock, so render_tty/render_non_tty must NOT lock again
    if (enabled_) {
        try {
            if (is_tty_) render_tty();
            else render_non_tty();
        } catch (...) {}
    }
}

void ProgressReporter::finish() noexcept {
    finished_ = true;
    try {
        if (enabled_) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (is_tty_) render_tty();
            else render_non_tty();
            std::fprintf(stderr, "\n");
            std::fflush(stderr);
        }
    } catch (...) {
        // best-effort; avoid std::terminate
    }
}

void ProgressReporter::render() noexcept {
    try {
        // Lock here, then call the unlocked render_* variants
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_tty_) render_tty();
        else render_non_tty();
    } catch (...) {
        // best-effort; avoid std::terminate
    }
}

void ProgressReporter::render_tty() {
    // NOTE: mutex_ is already held by caller - do NOT lock here (would deadlock)
    std::size_t cur = current_.load(std::memory_order_relaxed);
    double pct = (total_ > 0) ? (static_cast<double>(cur) / static_cast<double>(total_)) * 100.0 : 0.0;

    // Bar width: leave room for "XXX% [bar] N/M label"
    int label_max = std::max(0, tty_width_ - 50);
    std::string label = current_label_;
    if (static_cast<int>(label.size()) > label_max) {
        label = label.substr(0, static_cast<std::size_t>(std::max(0, label_max - 3))) + "...";
    }

    int bar_width = std::min(40, std::max(10, tty_width_ - static_cast<int>(label.size()) - 25));
    int filled = static_cast<int>((pct / 100.0) * bar_width);

    // Carriage return to overwrite previous line
    std::fprintf(stderr, "\r\033[K");  // clear line
    std::fprintf(stderr, "%3zu%% [", static_cast<std::size_t>(pct));
    for (int i = 0; i < bar_width; ++i) {
        std::fputc(i < filled ? '#' : '-', stderr);
    }
    std::fprintf(stderr, "] %zu/%zu %s",
                 cur, total_, label.c_str());
    std::fflush(stderr);
}

void ProgressReporter::render_non_tty() {
    // NOTE: mutex_ is already held by caller - do NOT lock here (would deadlock)
    std::size_t cur = current_.load(std::memory_order_relaxed);
    if (cur == last_printed_) return;
    last_printed_ = cur;
    std::fprintf(stderr, "  [%zu/%zu] %s\n", cur, total_, current_label_.c_str());
}

} // namespace xbox::cli
