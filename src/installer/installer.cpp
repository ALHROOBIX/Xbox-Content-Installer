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
#include "xbox/installer/installer.hpp"

#include "xbox/core/errors.hpp"
#include "xbox/core/logger.hpp"
#include "xbox/io/file_io.hpp"
#include "xbox/io/memory_map.hpp"
#include "xbox/stfs/stfs_extractor.hpp"
#include "xbox/stfs/stfs_metadata.hpp"
#include "xbox/utils/path_utils.hpp"
#include "xbox/utils/string_utils.hpp"
#include "xbox/concurrency/thread_pool.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <future>
#include <memory>
#include <optional>
#include <sstream>

namespace xbox::installer {

namespace fs = std::filesystem;

std::string PackageInstallResult::format_summary() const {
    std::ostringstream oss;
    oss << source_path.string() << " -> "
        << location.content_id_dir.string();
    if (skipped) {
        oss << " [SKIPPED: " << skip_reason << "]";
    } else if (!error.empty()) {
        oss << " [FAILED: " << error << "]";
    } else {
        oss << " [OK: " << extraction.format_summary() << "]";
    }
    return oss.str();
}

std::string InstallReport::format_summary() const {
    std::ostringstream oss;
    oss << "Installed " << succeeded << "/" << packages.size()
        << " packages (" << failed << " failed, " << skipped << " skipped)"
        << ", " << total_bytes_written << " bytes written";
    auto ms = total_elapsed.count();
    if (ms > 0) {
        double mbps = (static_cast<double>(total_bytes_written) / (1024.0 * 1024.0)) /
                      (static_cast<double>(ms) / 1000.0);
        char buf[64];
        std::snprintf(buf, sizeof(buf), " in %.2fs (%.1f MB/s)",
                      static_cast<double>(ms) / 1000.0, mbps);
        oss << buf;
    }
    return oss.str();
}

namespace {

// Convert CLI conflict policy string to enum
ConflictPolicy parse_conflict_policy(std::string_view s) {
    if (s == "overwrite") return ConflictPolicy::Overwrite;
    if (s == "skip")      return ConflictPolicy::Skip;
    if (s == "rename")    return ConflictPolicy::Rename;
    if (s == "fail")      return ConflictPolicy::Fail;
    return ConflictPolicy::Skip;  // safe default
}

// Write a u32 in big-endian to a buffer
void write_u32_be(std::vector<u8>& buf, std::size_t offset, u32 v) {
    buf[offset + 0] = static_cast<u8>((v >> 24) & 0xFF);
    buf[offset + 1] = static_cast<u8>((v >> 16) & 0xFF);
    buf[offset + 2] = static_cast<u8>((v >>  8) & 0xFF);
    buf[offset + 3] = static_cast<u8>( v        & 0xFF);
}

// Write a u64 in big-endian to a buffer
void write_u64_be(std::vector<u8>& buf, std::size_t offset, u64 v) {
    for (int i = 0; i < 8; ++i) {
        buf[offset + i] = static_cast<u8>((v >> (56 - 8*i)) & 0xFF);
    }
}

// Encode a UTF-8 string to UTF-16BE and write to buffer (fixed size, null-padded)
void write_utf16be(std::vector<u8>& buf, std::size_t offset, std::size_t max_chars,
                   const std::string& utf8) {
    // Simple UTF-8 to UTF-16BE conversion (BMP only, no surrogates)
    std::size_t pos = 0;
    std::size_t i = 0;
    while (pos < utf8.size() && i < max_chars) {
        char c = utf8[pos];
        u16 cp;
        if ((c & 0x80) == 0) {
            cp = static_cast<u16>(c);
            ++pos;
        } else if ((c & 0xE0) == 0xC0) {
            cp = (static_cast<u16>(c & 0x1F) << 6);
            if (pos + 1 < utf8.size()) {
                cp |= static_cast<u16>(utf8[pos + 1] & 0x3F);
                pos += 2;
            } else { break; }
        } else {
            // Skip multi-byte sequences we can't handle simply
            ++pos;
            continue;
        }
        buf[offset + i * 2]     = static_cast<u8>((cp >> 8) & 0xFF);
        buf[offset + i * 2 + 1] = static_cast<u8>( cp       & 0xFF);
        ++i;
    }
    // Remaining bytes are already 0 (null-padded)
}

// Write an ASCII string to buffer (fixed size, null-padded)
void write_ascii(std::vector<u8>& buf, std::size_t offset, std::size_t max_len,
                 const std::string& str) {
    std::size_t len = std::min(str.size(), max_len);
    std::memcpy(buf.data() + offset, str.data(), len);
    // Remaining bytes are already 0
}

} // namespace

// ---------------------------------------------------------------------------
// Write a .header file for an installed package (Xenia metadata)
// Per Xenia's virtual_file_system.cc: ExtractContentHeader
// File layout: XCONTENT_AGGREGATE_DATA (0x148 bytes) + license_mask (4 bytes)
// 
// XCONTENT_AGGREGATE_DATA structure (verified against Xenia's actual output):
//   0x000: device_id (uint32_be) = 1
//   0x004: content_type (uint32_be)
//   0x008: display_name (UTF-16BE, 128 chars = 256 bytes)
//   0x108: file_name (ASCII, 42 bytes) - FULL filename with extension!
//   0x132: padding (2 bytes)
//   0x134: xuid (uint64_be)
//   0x13C: unknown/reserved (4 bytes) = 0
//   0x140: title_id (uint32_be)
//   0x144: unknown/reserved (4 bytes) = 0
//   Total: 0x148 (328 bytes)
// + license_mask (4 bytes) at 0x148
// Grand total: 0x14C (332 bytes)
// ---------------------------------------------------------------------------
Result<fs::path, Error> write_header_file(
    const stfs::StfsHeader& header,
    const InstallLocation& loc,
    const fs::path& source_filename) {

    constexpr std::size_t HEADER_FILE_SIZE = 0x14C;
    std::vector<u8> buf(HEADER_FILE_SIZE, 0);

    // Offset 0x000: device_id (uint32_be) = 1 (always 1 for HDD)
    write_u32_be(buf, 0x000, 1);

    // Offset 0x004: content_type (uint32_be)
    write_u32_be(buf, 0x004, header.content_type);

    // Offset 0x008: display_name (UTF-16BE, 128 chars = 256 bytes)
    write_utf16be(buf, 0x008, 128, header.display_name);

    // Offset 0x108: file_name (ASCII, 42 bytes)
    // IMPORTANT: Use the FULL source filename (with extension), not the stripped one!
    // Xenia stores the complete original filename including all dots and extensions
    std::string full_filename = source_filename.filename().string();
    write_ascii(buf, 0x108, 42, full_filename);

    // Offset 0x132: padding (2 bytes) = 0 (already zero)
    // Offset 0x134: xuid (uint64_be)
    write_u64_be(buf, 0x134, loc.xuid);

    // Offset 0x13C: unknown/reserved (4 bytes) = 0 (already zero)
    // Offset 0x140: title_id (uint32_be) - NOTE: at 0x140, not 0x13C!
    write_u32_be(buf, 0x140, header.title_id);

    // Offset 0x144: unknown/reserved (4 bytes) = 0 (already zero)
    // Offset 0x148: license_mask (uint32_be)
    write_u32_be(buf, 0x148, header.license_mask());

    // Compute the header file path
    // content_root/<xuid>/<title_id>/Headers/<content_type>/<file_name>.header
    fs::path header_file = loc.title_dir / "Headers" /
                           path::format_content_type(loc.content_type) /
                           (loc.file_name + ".header");

    // Ensure the parent directory exists
    auto parent = header_file.parent_path();
    if (!parent.empty()) {
        auto r = path::ensure_directory(parent);
        if (!r.is_ok()) {
            return std::move(r).error();
        }
    }

    // Write the file atomically
    auto write_r = path::atomic_write_file(header_file, buf.data(), buf.size());
    if (!write_r.is_ok()) {
        return std::move(write_r).error();
    }

    XBOX_LOG_INFO("Wrote header file: {}", header_file.string());
    return header_file;
}

Result<PackageInstallResult, Error> install_package(
    const fs::path& stfs_file,
    const PathResolver& resolver,
    const InstallOptions& opts) {

    PackageInstallResult result;
    result.source_path = stfs_file;
    auto start = std::chrono::steady_clock::now();

    // Open and parse the STFS package
    stfs::StfsReader reader;
    XBOX_TRY_ASSIGN(reader, stfs::StfsReader::open(stfs_file, opts.no_mmap));

    const auto& header = reader.header();
    XBOX_LOG_DEBUG("Opened STFS: title_id={:08X}, content_type={:08X} ({}), {} files",
                   header.title_id, header.content_type,
                   header.content_type_name(),
                   reader.file_entries().size());

    // Optional: verify content type is one we know how to handle
    if (!opts.allow_unknown_content_type) {
        bool known = (header.content_type == content_type::TITLE_UPDATE) ||
                     (header.content_type == content_type::MARKETPLACE_CONTENT) ||
                     (header.content_type == content_type::SAVED_GAME) ||
                     (header.content_type == 0x00010000u) ||  // Profile
                     (header.content_type == content_type::ARCADE_TITLE) ||
                     (header.content_type == content_type::INSTALLED_GAME) ||
                     (header.content_type == content_type::XBOX_360_TITLE) ||
                     (header.content_type == content_type::AVATAR_ITEM) ||
                     (header.content_type == content_type::GAME_DEMO) ||
                     (header.content_type == content_type::GAME_TITLE);
        if (!known) {
            XBOX_LOG_WARN("Unknown content type 0x{:08X} - installing anyway (--allow-unknown)",
                          header.content_type);
        }
    }

    // Resolve install location (Xenia-style path using file_name, NOT content_id)
    InstallLocation loc;
    XBOX_TRY_ASSIGN(loc, resolver.resolve(header, stfs_file.filename().string()));
    result.location = loc;

    // Check if this is an SVOD container (multi-file, large DLC)
    result.is_svod = header.is_svod();

    // Verify block hashes if requested.
    // CRITICAL: Matches Xenia Canary behavior. Xenia does NOT verify SHA1 at all.
    // We check only file-chain blocks (not free/unused) and log WARNING on mismatch
    // (do NOT abort). Many valid TU/DLC files have patched blocks whose SHA1 differs.
    if (opts.verify) {
        XBOX_LOG_DEBUG("Verifying STFS file-chain block hashes for {}...", stfs_file.string());
        stfs::StfsReader::VerifyReport report;
        XBOX_TRY_ASSIGN(report, reader.verify_file_chain_blocks());
        if (report.failed > 0) {
            XBOX_LOG_WARN("STFS verification: {} of {} blocks failed SHA1 check "
                          "(continuing anyway, matches Xenia behavior)",
                          report.failed, report.total_blocks);
        } else {
            XBOX_LOG_DEBUG("STFS verification: {} blocks OK (file chains only)",
                           report.verified_ok);
        }
    }

    // Check if destination already exists - apply conflict policy
    if (fs::exists(loc.content_id_dir)) {
        bool proceed;
        XBOX_TRY_ASSIGN(proceed, resolve_conflict(loc.content_id_dir, opts.conflict));
        if (!proceed) {
            result.skipped = true;
            result.skip_reason = "already installed";
            result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            return result;
        }
    }

    // Dry-run: don't extract, just report what would happen
    if (opts.dry_run) {
        stfs::ExtractOptions eopts{};
        eopts.verify = false;
        eopts.dry_run = true;
        eopts.max_workers = 1;
        stfs::ExtractReport er;
        XBOX_TRY_ASSIGN(er, stfs::extract_all(reader, loc.content_id_dir, eopts));
        result.extraction = std::move(er);
        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        return result;
    }

    // Ensure destination directory exists
    XBOX_TRY(path::ensure_directory(loc.content_id_dir));

    // Extract
    stfs::ExtractOptions eopts{};
    eopts.verify = false;  // already verified above
    eopts.overwrite = (opts.conflict == ConflictPolicy::Overwrite);
    eopts.max_workers = opts.workers_per_package;
    eopts.dry_run = opts.dry_run;
    if (opts.on_progress) {
        std::size_t total_files = reader.file_entries().size();
        eopts.on_progress = [opts, total_files, &stfs_file](
            std::size_t cur, std::size_t total, std::string_view path) {
            opts.on_progress(cur, total, path, stfs_file.string());
        };
    }

    auto extract_r = stfs::extract_all(reader, loc.content_id_dir, eopts);
    if (!extract_r.is_ok()) {
        result.error = extract_r.error().message();
        result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        return result;
    }
    result.extraction = std::move(extract_r).value();

    // Write the .header file for Xenia compatibility
    // This allows Xenia to read the correct display_name and license_mask
    if (!opts.dry_run) {
        auto header_r = write_header_file(header, loc, stfs_file);
        if (!header_r.is_ok()) {
            XBOX_LOG_WARN("Failed to write .header file: {}", header_r.error().message());
            // Non-fatal - the content will still work, just without proper metadata
        }
    }

    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    XBOX_LOG_INFO("Installed {} -> {} ({} files, {} bytes)",
                  stfs_file.string(), loc.content_id_dir.string(),
                  result.extraction.files_extracted,
                  result.extraction.total_bytes_written);

    return result;
}

Result<InstallReport, Error> install_packages(
    const std::vector<fs::path>& stfs_files,
    const PathResolver& resolver,
    const InstallOptions& opts) {

    InstallReport report;
    auto start = std::chrono::steady_clock::now();

    if (stfs_files.empty()) {
        return XBOX_CLI_ERROR(MissingRequiredArgument, "no input files");
    }

    report.packages.reserve(stfs_files.size());

    // Single-package or sequential mode
    if (opts.parallel_packages <= 1 || stfs_files.size() == 1) {
        for (const auto& f : stfs_files) {
            auto r = install_package(f, resolver, opts);
            if (!r.is_ok()) {
                PackageInstallResult failed;
                failed.source_path = f;
                failed.error = r.error().message();
                report.packages.push_back(std::move(failed));
                ++report.failed;
            } else {
                auto& pkg = r.value();
                if (pkg.skipped) ++report.skipped;
                else if (!pkg.error.empty()) ++report.failed;
                else {
                    ++report.succeeded;
                    report.total_bytes_written += pkg.extraction.total_bytes_written;
                }
                report.packages.push_back(std::move(pkg));
            }
        }
    } else {
        // Parallel package install
        concurrency::ThreadPool pool(opts.parallel_packages);
        std::vector<std::future<Result<PackageInstallResult, Error>>> futures;
        futures.reserve(stfs_files.size());

        for (const auto& f : stfs_files) {
            auto fut = pool.submit([&resolver, &opts, f]() {
                return install_package(f, resolver, opts);
            });
            futures.push_back(std::move(fut));
        }

        for (auto& fut : futures) {
            auto r = fut.get();
            if (!r.is_ok()) {
                PackageInstallResult failed;
                failed.error = r.error().message();
                report.packages.push_back(std::move(failed));
                ++report.failed;
            } else {
                auto& pkg = r.value();
                if (pkg.skipped) ++report.skipped;
                else if (!pkg.error.empty()) ++report.failed;
                else {
                    ++report.succeeded;
                    report.total_bytes_written += pkg.extraction.total_bytes_written;
                }
                report.packages.push_back(std::move(pkg));
            }
        }
    }

    report.total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return report;
}

Result<void, Error> uninstall_package(
    u32 title_id,
    std::string_view file_name_or_prefix,
    const PathResolver& resolver,
    std::optional<u64> xuid_filter) {

    PathResolver::InstalledEntry entry;
    if (xuid_filter) {
        XBOX_TRY_ASSIGN(entry, resolver.find_installed(title_id, file_name_or_prefix, *xuid_filter));
    } else {
        XBOX_TRY_ASSIGN(entry, resolver.find_installed(title_id, file_name_or_prefix));
    }
    XBOX_LOG_INFO("Uninstalling: {}", entry.path.string());

    // Remove the content directory
    path::RemovalReport report;
    XBOX_TRY_ASSIGN(report, path::recursive_remove_all(entry.path));
    if (!report.failed_paths.empty()) {
        return XBOX_INSTALL_ERROR(FileWriteFailed,
            "uninstall incomplete: " + std::to_string(report.failed_paths.size()) +
            " items could not be removed");
    }

    // Also remove the .header file if it exists
    // Path: content_root/<xuid>/<title_id>/Headers/<content_type>/<file_name>.header
    // entry.path = .../<title_id>/<content_type>/<file_name>
    // .parent() = .../<title_id>/<content_type>
    // .parent().parent() = .../<title_id>  ← this is where Headers/ goes
    auto header_file = entry.path.parent_path().parent_path() /
                       "Headers" /
                       path::format_content_type(entry.content_type) /
                       (entry.file_name + ".header");
    std::error_code ec;
    if (fs::exists(header_file, ec)) {
        fs::remove(header_file, ec);
        if (ec) {
            XBOX_LOG_WARN("Could not remove header file: {}", header_file.string());
        } else {
            XBOX_LOG_INFO("Removed header file: {}", header_file.string());
        }
    }

    return {};
}

Result<fs::path, Error> disable_package(
    u32 title_id,
    std::string_view file_name_or_prefix,
    const PathResolver& resolver,
    std::optional<u64> xuid_filter) {

    PathResolver::InstalledEntry entry;
    if (xuid_filter) {
        XBOX_TRY_ASSIGN(entry, resolver.find_installed(title_id, file_name_or_prefix, *xuid_filter));
    } else {
        XBOX_TRY_ASSIGN(entry, resolver.find_installed(title_id, file_name_or_prefix));
    }
    if (entry.is_disabled) {
        return XBOX_INSTALL_ERROR(InvalidState,
            "content is already disabled: " + entry.path.string());
    }

    auto disabled = resolver.disabled_path_for(entry);
    std::error_code ec;
    fs::rename(entry.path, disabled, ec);
    if (ec) {
        return XBOX_IO_ERROR(FileWriteFailed,
            "disable rename failed: " + ec.message());
    }
    XBOX_LOG_INFO("Disabled: {} -> {}", entry.path.string(), disabled.string());
    return disabled;
}

Result<fs::path, Error> enable_package(
    u32 title_id,
    std::string_view file_name_or_prefix,
    const PathResolver& resolver,
    std::optional<u64> xuid_filter) {

    // First find the (possibly-disabled) entry
    std::vector<PathResolver::InstalledEntry> entries;
    XBOX_TRY_ASSIGN(entries, resolver.list_installed_for_title(title_id));

    // Shared content types — these live under xuid=0000000000000000 but are
    // visible to ALL profiles. So if the user passes --xuid, we should still
    // find TU/DLC under xuid=0.
    auto is_shared_content = [](u32 content_type) {
        return content_type == 0x000B0000 ||  // Title Update
               content_type == 0x00000002 ||  // DLC / Marketplace
               content_type == 0x00007000 ||  // Xbox 360 Title
               content_type == 0x000D0000 ||  // Arcade Title
               content_type == 0x00040000;    // Game Demo
    };

    // Find by file_name (case-insensitive) - look for disabled entries
    // Apply xuid_filter if set
    std::string query_lower = str::to_lower(std::string(file_name_or_prefix));
    std::vector<const PathResolver::InstalledEntry*> matches;
    for (const auto& e : entries) {
        if (!e.is_disabled) continue;

        // XUID filter: skip entries that don't match
        // Shared content (TU/DLC) is always included regardless of xuid
        if (xuid_filter && e.xuid != *xuid_filter && !is_shared_content(e.content_type)) {
            continue;
        }

        std::string fn_lower = str::to_lower(e.file_name);
        if (fn_lower == query_lower || str::starts_with(fn_lower, query_lower)) {
            matches.push_back(&e);
        }
    }

    if (matches.empty()) {
        std::string xuid_msg;
        if (xuid_filter) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%016llX",
                          static_cast<unsigned long long>(*xuid_filter));
            xuid_msg = " (xuid filter: " + std::string(buf) + ")";
        }
        return XBOX_INSTALL_ERROR(ContentNotInstalled,
            "no disabled content found for title " + path::format_title_id(title_id) +
            " with name/prefix '" + std::string(file_name_or_prefix) + "'" + xuid_msg);
    }
    if (matches.size() > 1) {
        std::string detail;
        for (std::size_t i = 0; i < matches.size() && i < 5; ++i) {
            const auto* m = matches[i];
            if (i > 0) detail += ", ";
            detail += "xuid=" + PathResolver::format_xuid(m->xuid) + " ct=" +
                      str::format_hex_u32(m->content_type) + " name=" + m->file_name;
        }
        return XBOX_INSTALL_ERROR(ConflictResolutionFailed,
            "ambiguous file name '" + std::string(file_name_or_prefix) +
            "' matches " + std::to_string(matches.size()) + " disabled packages: " + detail);
    }

    const auto& e = *matches[0];
    // Strip the .disabled suffix to get the enabled path
    auto enabled = e.path.parent_path() / e.file_name;
    std::error_code ec;
    fs::rename(e.path, enabled, ec);
    if (ec) {
        return XBOX_IO_ERROR(FileWriteFailed,
            "enable rename failed: " + ec.message());
    }
    XBOX_LOG_INFO("Enabled: {} -> {}", e.path.string(), enabled.string());
    return enabled;
}

} // namespace xbox::installer
