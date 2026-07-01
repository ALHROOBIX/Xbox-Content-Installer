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
#include "xbox/stfs/stfs_extractor.hpp"

#include "xbox/core/errors.hpp"
#include "xbox/core/logger.hpp"
#include "xbox/io/buffered_writer.hpp"
#include "xbox/utils/path_utils.hpp"
#include "xbox/utils/string_utils.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace xbox::stfs {

namespace fs = std::filesystem;

std::string ExtractReport::format_summary() const {
    std::string out;
    out.reserve(256);
    out.append("Extraction").append(dry_run ? " (dry-run)" : "").append(": ");
    out.append(std::to_string(files_extracted)).append(" extracted, ");
    out.append(std::to_string(files_skipped)).append(" skipped, ");
    out.append(std::to_string(files_failed)).append(" failed, ");
    out.append(std::to_string(total_bytes_written)).append(" bytes written");
    auto ms = elapsed.count();
    if (ms > 0) {
        double mbps = (static_cast<double>(total_bytes_written) / (1024.0 * 1024.0)) /
                      (static_cast<double>(ms) / 1000.0);
        char buf[64];
        std::snprintf(buf, sizeof(buf), " in %.2fs (%.1f MB/s)",
                      static_cast<double>(ms) / 1000.0, mbps);
        out.append(buf);
    }
    return out;
}

namespace {

// Build the full output path for a file entry by walking up its parents.
// Returns nullopt if the path would escape the dest_dir (safety check).
std::optional<fs::path> build_output_path(
    const StfsReader& reader, const FileEntry& entry,
    const fs::path& dest_dir) {
    // Collect path components
    std::vector<std::string> parts;
    const FileEntry* current = &entry;
    while (current && current->path_indicator != -1) {
        parts.push_back(str::sanitize_filename(current->name));
        std::size_t parent_idx = static_cast<std::size_t>(current->path_indicator);
        if (parent_idx >= reader.file_entries().size()) break;
        current = &reader.file_entries()[parent_idx];
    }
    if (current) parts.push_back(str::sanitize_filename(current->name));
    std::reverse(parts.begin(), parts.end());

    auto joined = xbox::path::safe_join_many(dest_dir, parts);
    return joined;
}

} // namespace

Result<ExtractedFile, Error> extract_single(
    const StfsReader& reader,
    const FileEntry& entry,
    const fs::path& output_path,
    const ExtractOptions& opts) {
    ExtractedFile result;
    result.relative_path = xbox::path::display_path(output_path);

    if (entry.is_directory()) {
        // Directories are created on-demand by their children
        return result;
    }

    if (fs::exists(output_path)) {
        if (!opts.overwrite) {
            result.skipped = true;
            return result;
        }
    }

    if (opts.dry_run) {
        result.size = entry.file_size;
        return result;
    }

    // Ensure parent directory exists
    auto parent = output_path.parent_path();
    if (!parent.empty()) {
        auto r = xbox::path::ensure_directory(parent);
        if (!r.is_ok()) {
            result.error = r.error().message();
            return result;
        }
    }

    // Read the file from the STFS package
    auto data = reader.read_file(entry, opts.verify);
    if (!data.is_ok()) {
        result.error = data.error().message();
        return Err<Error>{xbox::Error{ErrorCode::FileReadFailed, ErrorCategory::Stfs, result.error}};
    }
    result.verified = opts.verify;

    // Write atomically: temp file + rename
    auto write_r = xbox::path::atomic_write_file(output_path,
        data.value().data(), data.value().size());
    if (!write_r.is_ok()) {
        result.error = write_r.error().message();
        return result;
    }

    result.size = data.value().size();
    return result;
}

Result<ExtractReport, Error> extract_all(
    const StfsReader& reader,
    const fs::path& dest_dir,
    const ExtractOptions& opts) {
    ExtractReport report;
    report.dry_run = opts.dry_run;
    auto start = std::chrono::steady_clock::now();

    // Collect all file entries (skip directories)
    std::vector<const FileEntry*> files_to_extract;
    for (const auto& e : reader.file_entries()) {
        if (!e.is_directory()) {
            files_to_extract.push_back(&e);
        }
    }

    if (files_to_extract.empty()) {
        XBOX_LOG_WARN("STFS package contains no files");
        report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        return report;
    }

    XBOX_LOG_INFO("Extracting {} files to {} ({} workers, verify={})",
                  files_to_extract.size(), dest_dir.string(),
                  opts.max_workers, opts.verify);

    // Create the destination directory if it doesn't exist
    auto dir_r = xbox::path::ensure_directory(dest_dir);
    if (!dir_r.is_ok()) {
        return std::move(dir_r).error();
    }

    // Pre-compute output paths for each file (so workers don't need to walk
    // the parent tree repeatedly)
    std::vector<std::pair<const FileEntry*, fs::path>> work_items;
    work_items.reserve(files_to_extract.size());
    for (auto* e : files_to_extract) {
        auto op = build_output_path(reader, *e, dest_dir);
        if (!op) {
            ExtractedFile failed;
            failed.relative_path = e->name;
            failed.error = "path traversal detected - refused to extract";
            report.failed.push_back(failed);
            ++report.files_failed;
            continue;
        }
        work_items.emplace_back(e, *op);
    }

    // Sequential extraction if max_workers <= 1, otherwise use a thread pool
    if (opts.max_workers <= 1) {
        for (std::size_t i = 0; i < work_items.size(); ++i) {
            const auto& [entry, output_path] = work_items[i];
            auto r = extract_single(reader, *entry, output_path, opts);
            if (!r.is_ok()) {
                ExtractedFile failed;
                failed.relative_path = xbox::path::display_path(output_path);
                failed.error = r.error().message();
                report.failed.push_back(failed);
                ++report.files_failed;
            } else {
                auto& ef = r.value();
                if (ef.skipped) {
                    ++report.files_skipped;
                    report.total_bytes_skipped += entry->file_size;
                } else {
                    ++report.files_extracted;
                    report.total_bytes_written += ef.size;
                }
                report.files.push_back(std::move(ef));
            }
            if (opts.on_progress) {
                opts.on_progress(i + 1, work_items.size(), output_path.string());
            }
        }
    } else {
        // Thread pool extraction
        concurrency::ThreadPool pool(opts.max_workers);

        std::atomic<std::size_t> completed{0};
        std::atomic<std::size_t> failed{0};
        std::atomic<std::size_t> skipped{0};
        std::atomic<u64> bytes_written{0};
        std::atomic<u64> bytes_skipped{0};

        std::mutex results_mutex;
        std::vector<ExtractedFile> all_results;
        std::vector<ExtractedFile> all_failures;

        std::vector<std::future<void>> futures;
        futures.reserve(work_items.size());

        for (const auto& [entry, output_path] : work_items) {
            auto fut = pool.submit([&reader, entry, output_path, &opts,
                                    &completed, &failed, &skipped,
                                    &bytes_written, &bytes_skipped,
                                    &results_mutex, &all_results, &all_failures,
                                    total = work_items.size()]() {
                auto r = extract_single(reader, *entry, output_path, opts);
                if (!r.is_ok()) {
                    ExtractedFile ef;
                    ef.relative_path = xbox::path::display_path(output_path);
                    ef.error = r.error().message();
                    {
                        std::lock_guard<std::mutex> lock(results_mutex);
                        all_failures.push_back(std::move(ef));
                    }
                    failed.fetch_add(1, std::memory_order_relaxed);
                } else {
                    auto& ef = r.value();
                    if (ef.skipped) {
                        skipped.fetch_add(1, std::memory_order_relaxed);
                        bytes_skipped.fetch_add(entry->file_size, std::memory_order_relaxed);
                    } else {
                        bytes_written.fetch_add(ef.size, std::memory_order_relaxed);
                    }
                    {
                        std::lock_guard<std::mutex> lock(results_mutex);
                        all_results.push_back(std::move(ef));
                    }
                }
                std::size_t c = completed.fetch_add(1, std::memory_order_relaxed) + 1;
                if (opts.on_progress) {
                    opts.on_progress(c, total, output_path.string());
                }
            });
            futures.push_back(std::move(fut));
        }

        // Wait for all
        for (auto& f : futures) {
            try { f.wait(); } catch (...) {}
        }

        report.files_extracted = work_items.size() - failed.load() - skipped.load();
        report.files_skipped = skipped.load();
        report.files_failed = failed.load();
        report.total_bytes_written = bytes_written.load();
        report.total_bytes_skipped = bytes_skipped.load();
        report.files = std::move(all_results);
        report.failed = std::move(all_failures);
    }

    report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    return report;
}

} // namespace xbox::stfs
