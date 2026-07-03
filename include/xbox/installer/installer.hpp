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
#include "xbox/installer/conflict_policy.hpp"
#include "xbox/installer/path_resolver.hpp"
#include "xbox/stfs/stfs_extractor.hpp"
#include "xbox/stfs/stfs_reader.hpp"
#include "xbox/xiso/xiso_reader.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xbox::installer {

namespace fs = std::filesystem;

struct InstallOptions {
    // Verify block SHA1 hashes before extraction (default: true)
    bool verify{true};

    // Conflict policy when the destination already exists
    ConflictPolicy conflict{ConflictPolicy::Skip};

    // Number of parallel extraction workers (per package)
    std::size_t workers_per_package{4};

    // Number of parallel packages processed at once (batch install)
    std::size_t parallel_packages{2};

    // Dry-run: report what would be done without writing
    bool dry_run{false};

    // Skip the title-update / DLC validity check (otherwise we reject
    // packages whose content type isn't in our known-good list).
    bool allow_unknown_content_type{false};

    // Force buffer reading instead of mmap (for NTFS/FUSE filesystems)
    bool no_mmap{false};

    // SVOD extraction mode:
    //   false (default) = just copy .data directory + write header (Xenia-correct)
    //   true             = also extract files via binary tree (for inspection)
    // Default is false because Xenia reads SVOD data directly from .data fragments
    // during emulation — extracting files is redundant and wastes RAM/disk.
    bool extract_svod_files{false};

    // Progress callback: (current_file, total_files, current_path, current_package)
    using ProgressFn = std::function<void(
        std::size_t current_file, std::size_t total_files,
        std::string_view current_path,
        std::string_view current_package)>;
    ProgressFn on_progress;
};

struct PackageInstallResult {
    fs::path source_path;             // input STFS file
    InstallLocation location;          // resolved install location
    stfs::ExtractReport extraction;    // extraction details
    bool skipped{false};               // skipped due to conflict policy
    bool is_svod{false};               // was this an SVOD container?
    std::string skip_reason;           // empty if not skipped
    std::string error;                 // empty on success
    std::chrono::milliseconds elapsed{};

    [[nodiscard]] bool success() const noexcept { return error.empty() && !skipped; }

    [[nodiscard]] std::string format_summary() const;
};

struct InstallReport {
    std::vector<PackageInstallResult> packages{};
    std::size_t succeeded{0};
    std::size_t failed{0};
    std::size_t skipped{0};
    u64 total_bytes_written{0};
    std::chrono::milliseconds total_elapsed{};

    [[nodiscard]] std::string format_summary() const;
};

// Install a single STFS package.
[[nodiscard]] Result<PackageInstallResult, Error> install_package(
    const fs::path& stfs_file,
    const PathResolver& resolver,
    const InstallOptions& opts = {});

// Write a .header file for an installed package (Xenia metadata)
// Creates: content_root/<xuid>/<title_id>/Headers/<content_type>/<file_name>.header
// Returns the path to the written header file
[[nodiscard]] Result<fs::path, Error> write_header_file(
    const stfs::StfsHeader& header,
    const InstallLocation& loc,
    const fs::path& source_filename);

// Install multiple STFS packages in parallel.
// Each package is processed independently; failures don't abort others.
[[nodiscard]] Result<InstallReport, Error> install_packages(
    const std::vector<fs::path>& stfs_files,
    const PathResolver& resolver,
    const InstallOptions& opts = {});

// Uninstall a previously-installed package.
// If xuid_filter is set, only searches under that XUID's directory (plus
// shared TU/DLC content under xuid=0). This is essential for saves which
// can be installed under multiple profiles simultaneously.
[[nodiscard]] Result<void, Error> uninstall_package(
    u32 title_id,
    std::string_view content_id_prefix,
    const PathResolver& resolver,
    std::optional<u64> xuid_filter = std::nullopt);

// Disable a package (rename its directory to .disabled).
// Same xuid_filter semantics as uninstall_package.
[[nodiscard]] Result<fs::path, Error> disable_package(
    u32 title_id,
    std::string_view content_id_prefix,
    const PathResolver& resolver,
    std::optional<u64> xuid_filter = std::nullopt);

// Re-enable a previously-disabled package.
// Same xuid_filter semantics as uninstall_package.
[[nodiscard]] Result<fs::path, Error> enable_package(
    u32 title_id,
    std::string_view content_id_prefix,
    const PathResolver& resolver,
    std::optional<u64> xuid_filter = std::nullopt);

// Install an ISO file (XISO disc image).
// Requires --extract-svod flag. Extracts all files including default.xex.
[[nodiscard]] Result<PackageInstallResult, Error> install_iso_package(
    const fs::path& file_path,
    const PathResolver& resolver,
    const InstallOptions& opts,
    std::chrono::steady_clock::time_point start);

} // namespace xbox::installer
