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
#include "xbox/installer/conflict_policy.hpp"

#include "xbox/core/errors.hpp"
#include "xbox/core/logger.hpp"
#include "xbox/utils/path_utils.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>
#include <sstream>
#include <system_error>

namespace xbox::installer {

namespace fs = std::filesystem;

std::string_view conflict_policy_name(ConflictPolicy p) noexcept {
    switch (p) {
        case ConflictPolicy::Overwrite: return "overwrite";
        case ConflictPolicy::Skip:      return "skip";
        case ConflictPolicy::Rename:    return "rename";
        case ConflictPolicy::Fail:      return "fail";
        case ConflictPolicy::Prompt:    return "prompt";
    }
    return "unknown";
}

namespace {
std::string make_timestamp_suffix() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}
} // namespace

Result<bool, Error> resolve_conflict(
    const fs::path& existing_dir,
    ConflictPolicy policy,
    PromptCallback prompt) {

    if (!fs::exists(existing_dir)) {
        // No conflict - proceed
        return true;
    }

    switch (policy) {
        case ConflictPolicy::Overwrite: {
            XBOX_LOG_WARN("Overwriting existing directory: {}", existing_dir.string());
            path::RemovalReport report;
            XBOX_TRY_ASSIGN(report, path::recursive_remove_all(existing_dir));
            if (!report.failed_paths.empty()) {
                return XBOX_INSTALL_ERROR(ConflictResolutionFailed,
                    "failed to fully remove existing directory: " +
                    existing_dir.string() + " (" +
                    std::to_string(report.failed_paths.size()) + " failures)");
            }
            return true;
        }
        case ConflictPolicy::Skip: {
            XBOX_LOG_INFO("Skipping (already exists): {}", existing_dir.string());
            return false;
        }
        case ConflictPolicy::Rename: {
            auto backup = fs::path(std::string(existing_dir.string()) +
                                   ".bak-" + make_timestamp_suffix());
            std::error_code ec;
            fs::rename(existing_dir, backup, ec);
            if (ec) {
                return XBOX_INSTALL_ERROR(ConflictResolutionFailed,
                    "failed to rename existing directory: " + existing_dir.string() +
                    " -> " + backup.string() + ": " + ec.message());
            }
            XBOX_LOG_INFO("Renamed existing directory to: {}", backup.string());
            return true;
        }
        case ConflictPolicy::Fail: {
            return XBOX_INSTALL_ERROR(ContentAlreadyInstalled,
                "content already installed at: " + existing_dir.string());
        }
        case ConflictPolicy::Prompt: {
            if (!prompt) {
                // No prompt callback - default to skip
                XBOX_LOG_WARN("Prompt policy without callback - skipping");
                return false;
            }
            if (prompt(existing_dir)) {
                path::RemovalReport report;
                XBOX_TRY_ASSIGN(report, path::recursive_remove_all(existing_dir));
                if (!report.failed_paths.empty()) {
                    return XBOX_INSTALL_ERROR(ConflictResolutionFailed,
                        "failed to fully remove existing directory");
                }
                return true;
            }
            return false;
        }
    }
    return true;
}

} // namespace xbox::installer
