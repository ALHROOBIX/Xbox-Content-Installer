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

#include "xbox/core/result.hpp"
#include "xbox/core/types.hpp"
#include "xbox/concurrency/thread_pool.hpp"
#include "xbox/stfs/stfs_reader.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace xbox::stfs {

namespace fs = std::filesystem;

struct ExtractOptions {
    // Whether to verify block SHA1 hashes before writing to disk.
    bool verify{true};

    // Whether to overwrite existing files (false = skip existing).
    bool overwrite{false};

    // Maximum number of concurrent extraction workers.
    std::size_t max_workers{4};

    // Buffer size for the buffered writer (per worker).
    std::size_t write_buffer_size{256 * 1024};

    // Dry-run mode: compute what would be extracted but don't write.
    bool dry_run{false};

    // Progress callback (current, total, current_path)
    std::function<void(std::size_t current, std::size_t total,
                       std::string_view current_path)> on_progress;
};

struct ExtractedFile {
    std::string relative_path;       // path relative to extraction root
    u64         size{0};             // bytes written
    bool        verified{false};     // SHA1 verified?
    bool        skipped{false};      // skipped (already existed, no overwrite)
    std::string error;               // empty on success
};

struct ExtractReport {
    std::vector<ExtractedFile> files{};
    std::vector<ExtractedFile> failed{};
    u64 total_bytes_written{0};
    u64 total_bytes_skipped{0};
    std::chrono::milliseconds elapsed{};
    std::size_t files_extracted{0};
    std::size_t files_skipped{0};
    std::size_t files_failed{0};
    bool        dry_run{false};

    [[nodiscard]] std::string format_summary() const;
};

// Extract all files from an STFS reader to a destination directory.
// `dest_dir` must exist (call ensure_directory() first if needed).
[[nodiscard]] Result<ExtractReport, Error> extract_all(
    const StfsReader& reader,
    const fs::path& dest_dir,
    const ExtractOptions& opts = {});

// Extract a single file from the reader to a specific output path.
[[nodiscard]] Result<ExtractedFile, Error> extract_single(
    const StfsReader& reader,
    const FileEntry& entry,
    const fs::path& output_path,
    const ExtractOptions& opts = {});

} // namespace xbox::stfs
