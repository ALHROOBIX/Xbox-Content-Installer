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
#include "xbox/stfs/stfs_header.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xbox::installer {

namespace fs = std::filesystem;

// Resolved install location for a single STFS package.
struct InstallLocation {
    fs::path root_dir;            // <content_root>
    fs::path xuid_dir;            // <root>/<xuid_hex>
    fs::path title_dir;           // <root>/<xuid_hex>/<title_id_hex>
    fs::path content_type_dir;    // <root>/<xuid_hex>/<title_id_hex>/<content_type_hex>
    fs::path content_id_dir;      // <root>/<xuid_hex>/<title_id_hex>/<content_type_hex>/<file_name>
    u32      title_id{0};
    u32      content_type{0};
    u64      xuid{0};             // profile_id from header (forced to 0 for DLC)
    std::string file_name;        // base name of the input STFS file (extension stripped)
    std::string content_id_hex;   // 40-char lowercase hex (kept for compatibility)
};

class PathResolver {
public:
    PathResolver() = default;
    explicit PathResolver(fs::path content_root)
        : content_root_(std::move(content_root)) {}

    // Set the content root directory (e.g. ~/.xenia/content or ./content).
    void set_root(fs::path p) { content_root_ = std::move(p); }
    [[nodiscard]] const fs::path& root() const noexcept { return content_root_; }

    // Set the xuid override (if called, forces install to this xuid)
    void set_xuid_override(u64 xuid) { xuid_override_ = xuid; xuid_override_set_ = true; }
    [[nodiscard]] u64 xuid_override() const noexcept { return xuid_override_; }
    [[nodiscard]] bool xuid_override_set() const noexcept { return xuid_override_set_; }

    // Compute the install location for a given STFS header + source file path.
    // The file_name is derived from `source_filename` (extension stripped).
    // Does NOT create any directories - use ensure_install_dir() for that.
    [[nodiscard]] Result<InstallLocation, Error> resolve(
        const stfs::StfsHeader& header,
        std::string_view source_filename) const;

    // Convenience: resolve from a raw header buffer (e.g. for quick listing).
    [[nodiscard]] Result<InstallLocation, Error> resolve_from_buffer(
        std::span<const byte> header_bytes,
        std::string_view source_filename) const;

    // Create the directory tree up to and including the content_id_dir.
    [[nodiscard]] Result<InstallLocation, Error> ensure_install_dir(
        const stfs::StfsHeader& header,
        std::string_view source_filename) const;

    // List all installed title IDs under the content root.
    [[nodiscard]] Result<std::vector<u32>, Error> list_installed_titles() const;

    // List all installed content packages for a given title ID.
    struct InstalledEntry {
        u32 title_id{0};
        u32 content_type{0};
        u64 xuid{0};
        std::string file_name;       // the package's directory name (file_name)
        std::string content_id_hex;  // empty if not derivable from path
        fs::path path;
        bool is_disabled{false};     // has .disabled suffix?
    };
    [[nodiscard]] Result<std::vector<InstalledEntry>, Error> list_installed_for_title(
        u32 title_id) const;

    // List ALL installed content across all titles.
    [[nodiscard]] Result<std::vector<InstalledEntry>, Error> list_all_installed() const;

    // Locate an installed package by title_id + file_name (or prefix).
    // Searches across ALL XUIDs (profiles). If multiple packages match,
    // returns an ambiguity error.
    [[nodiscard]] Result<InstalledEntry, Error> find_installed(
        u32 title_id, std::string_view file_name_or_prefix) const;

    // Locate an installed package by title_id + file_name (or prefix),
    // restricted to a specific XUID (profile). This is essential for
    // uninstall/disable/enable of save files, which can be installed
    // under multiple profiles simultaneously.
    //
    // If xuid_filter is set:
    //   - Only entries under that XUID's directory are considered
    //   - TU/DLC (shared content under xuid=0) are ALWAYS included
    //     because they are visible to all profiles
    [[nodiscard]] Result<InstalledEntry, Error> find_installed(
        u32 title_id, std::string_view file_name_or_prefix,
        u64 xuid_filter) const;

    // Compute the path that would be used for an "installed but disabled" package.
    [[nodiscard]] fs::path disabled_path(const InstallLocation& loc) const;

    // Compute the path for the .header file (Xenia's metadata file)
    // Path: content_root/<xuid>/<title_id>/Headers/<content_type>/<file_name>.header
    [[nodiscard]] fs::path header_path(const InstallLocation& loc) const;

    // Compute the path used for the disabled version of an InstalledEntry.
    [[nodiscard]] fs::path disabled_path_for(const InstalledEntry& e) const;

    // Whether an InstalledEntry is currently disabled.
    [[nodiscard]] static bool is_disabled(const InstalledEntry& e) noexcept;

private:
    fs::path content_root_{"content"};
    u64      xuid_override_{0};
    bool     xuid_override_set_{false};  // true if --xuid was explicitly provided

    // Compute the xuid to use for a given header (Xenia's DLC special case)
    [[nodiscard]] u64 compute_xuid(const stfs::StfsHeader& header) const noexcept;

    // Strip extension from a filename to get the package name
    [[nodiscard]] static std::string strip_extension(std::string_view filename);

public:
    // Convert a u64 to 16-char uppercase hex (Xenia's format)
    [[nodiscard]] static std::string format_xuid(u64 xuid) noexcept;
};

} // namespace xbox::installer
