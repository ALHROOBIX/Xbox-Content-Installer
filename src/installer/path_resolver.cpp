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
#include "xbox/installer/path_resolver.hpp"

#include "xbox/core/errors.hpp"
#include "xbox/core/logger.hpp"
#include "xbox/stfs/stfs_metadata.hpp"
#include "xbox/utils/path_utils.hpp"
#include "xbox/utils/string_utils.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace xbox::installer {

namespace fs = std::filesystem;

namespace {

// Read a u64 from an 8-byte big-endian array (for profile_id)
u64 read_be_u64(const u8* p) noexcept {
    u64 v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | p[i];
    }
    return v;
}

} // namespace

std::string PathResolver::format_xuid(u64 xuid) noexcept {
    // Per Xenia: fmt::format("{:016X}", xuid)
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(xuid));
    return buf;
}

std::string PathResolver::strip_extension(std::string_view filename) {
    // Get just the basename (in case a path was passed)
    auto last_sep = filename.find_last_of("/\\");
    std::string_view base = (last_sep != std::string_view::npos)
        ? filename.substr(last_sep + 1)
        : filename;

    // IMPORTANT: Do NOT strip any extension!
    // Xbox 360 STFS filenames use dots as version separators, NOT as
    // extension delimiters. E.g. "TU_16L61UL_0000004000000.0000000000181"
    // Xenia uses path.filename() which returns the FULL filename.
    return std::string(base);
}

u64 PathResolver::compute_xuid(const stfs::StfsHeader& header) const noexcept {
    // Per Xenia: if content_type == kMarketplaceContent (DLC), force xuid = 0
    // (Common directory shared across all profiles)
    if (header.content_type == content_type::MARKETPLACE_CONTENT) {
        // But if --xuid was explicitly set, honor it even for DLC
        if (xuid_override_set_) {
            return xuid_override_;
        }
        return 0;
    }

    // If --xuid override was explicitly set, use it
    if (xuid_override_set_) {
        return xuid_override_;
    }

    // Otherwise, use the profile_id from the header (offset 0x371, 8 bytes BE)
    return read_be_u64(header.profile_id.data());
}

Result<InstallLocation, Error> PathResolver::resolve(
    const stfs::StfsHeader& header,
    std::string_view source_filename) const {

    InstallLocation loc;
    loc.root_dir = content_root_;
    loc.title_id = header.title_id;
    loc.content_type = header.content_type;
    loc.xuid = compute_xuid(header);

    // File name: strip extension from source filename
    loc.file_name = str::sanitize_filename(strip_extension(source_filename));
    if (loc.file_name.empty()) {
        return XBOX_INSTALL_ERROR(InvalidContentId,
            "could not derive file_name from source path: " + std::string(source_filename));
    }

    // Content ID hex (kept for compatibility / info command, but not used in path)
    loc.content_id_hex = str::to_hex(header.content_id.data(), header.content_id.size());

    // Build the path: <root>/<xuid_hex>/<title_id_hex>/<content_type_hex>/<file_name>
    std::string xuid_str = format_xuid(loc.xuid);
    std::string title_id_str = path::format_title_id(header.title_id);
    std::string ct_str = path::format_content_type(header.content_type);

    loc.xuid_dir = content_root_ / xuid_str;
    loc.title_dir = loc.xuid_dir / title_id_str;
    loc.content_type_dir = loc.title_dir / ct_str;
    loc.content_id_dir = loc.content_type_dir / loc.file_name;

    return loc;
}

Result<InstallLocation, Error> PathResolver::resolve_from_buffer(
    std::span<const byte> header_bytes,
    std::string_view source_filename) const {
    stfs::StfsHeader h;
    XBOX_TRY_ASSIGN(h, stfs::parse_header(header_bytes));
    return resolve(h, source_filename);
}

Result<InstallLocation, Error> PathResolver::ensure_install_dir(
    const stfs::StfsHeader& header,
    std::string_view source_filename) const {
    InstallLocation loc;
    XBOX_TRY_ASSIGN(loc, resolve(header, source_filename));
    XBOX_TRY(path::ensure_directory(loc.content_id_dir));
    return loc;
}

// ---------------------------------------------------------------------------
// List installed content
// ---------------------------------------------------------------------------

namespace {

// Parse a 16-char hex string into a u64 (returns 0 on failure)
u64 parse_xuid_hex(std::string_view s) noexcept {
    if (s.size() != 16) return 0;
    u64 v = 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= static_cast<u64>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<u64>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= static_cast<u64>(c - 'A' + 10);
        else return 0;
    }
    return v;
}

} // namespace

Result<std::vector<u32>, Error> PathResolver::list_installed_titles() const {
    std::vector<u32> out;
    std::error_code ec;

    if (!fs::exists(content_root_, ec)) {
        return out;  // empty
    }

    // Walk: content_root/<xuid_hex>/<title_id_hex>/
    // We collect title IDs from ALL xuid directories
    for (auto& xuid_entry : fs::directory_iterator(content_root_, ec)) {
        if (!xuid_entry.is_directory(ec)) continue;
        auto xuid_name = xuid_entry.path().filename().string();
        if (xuid_name.size() != 16) continue;

        for (auto& title_entry : fs::directory_iterator(xuid_entry.path(), ec)) {
            if (!title_entry.is_directory(ec)) continue;
            auto name = title_entry.path().filename().string();
            if (name.size() != 8) continue;
            auto tid = str::parse_hex_u32(name);
            if (tid) {
                // Check if this title directory has any actual content
                // (subdirectories with packages, not just empty Headers/)
                bool has_content = false;
                for (auto& ct_entry : fs::directory_iterator(title_entry.path(), ec)) {
                    if (!ct_entry.is_directory(ec)) continue;
                    auto ct_name = ct_entry.path().filename().string();
                    if (ct_name == "Headers") continue;  // skip Headers directory
                    // Check if this content_type directory has any packages
                    for (auto& pkg_entry : fs::directory_iterator(ct_entry.path(), ec)) {
                        if (pkg_entry.is_directory(ec)) {
                            has_content = true;
                            break;
                        }
                    }
                    if (has_content) break;
                }
                if (has_content) {
                    // Avoid duplicates
                    if (std::find(out.begin(), out.end(), *tid) == out.end()) {
                        out.push_back(*tid);
                    }
                }
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

Result<std::vector<PathResolver::InstalledEntry>, Error> PathResolver::list_installed_for_title(
    u32 title_id) const {
    std::vector<InstalledEntry> out;
    std::error_code ec;

    if (!fs::exists(content_root_, ec)) return out;

    std::string title_id_str = path::format_title_id(title_id);

    // Walk: content_root/<xuid_hex>/<title_id_str>/<content_type_hex>/<file_name>/
    for (auto& xuid_entry : fs::directory_iterator(content_root_, ec)) {
        if (!xuid_entry.is_directory(ec)) continue;
        auto xuid_name = xuid_entry.path().filename().string();
        if (xuid_name.size() != 16) continue;
        u64 xuid = parse_xuid_hex(xuid_name);

        auto title_dir = xuid_entry.path() / title_id_str;
        if (!fs::exists(title_dir, ec)) continue;

        for (auto& ct_entry : fs::directory_iterator(title_dir, ec)) {
            if (!ct_entry.is_directory(ec)) continue;
            auto ct_name = ct_entry.path().filename().string();
            if (ct_name.size() != 8) continue;
            auto ct = str::parse_hex_u32(ct_name);
            if (!ct) continue;

            // The "00000002" subdirectory (Xenia uses this for marketplace content)
            // is NOT present in the Xenia layout - the file_name is a direct
            // child of <content_type_hex>/
            for (auto& pkg_entry : fs::directory_iterator(ct_entry.path(), ec)) {
                if (!pkg_entry.is_directory(ec)) continue;
                auto pkg_name = pkg_entry.path().filename().string();

                InstalledEntry ie;
                ie.title_id = title_id;
                ie.content_type = *ct;
                ie.xuid = xuid;
                ie.path = pkg_entry.path();

                // Check if disabled (has .disabled suffix)
                constexpr std::string_view DISABLED_SUFFIX = ".disabled";
                if (pkg_name.size() > DISABLED_SUFFIX.size() &&
                    str::ends_with(pkg_name, DISABLED_SUFFIX)) {
                    ie.is_disabled = true;
                    ie.file_name = pkg_name.substr(0,
                        pkg_name.size() - DISABLED_SUFFIX.size());
                } else {
                    ie.is_disabled = false;
                    ie.file_name = pkg_name;
                }
                out.push_back(std::move(ie));
            }
        }
    }
    return out;
}

Result<std::vector<PathResolver::InstalledEntry>, Error> PathResolver::list_all_installed() const {
    std::vector<InstalledEntry> out;
    std::vector<u32> titles;
    XBOX_TRY_ASSIGN(titles, list_installed_titles());
    for (auto tid : titles) {
        std::vector<InstalledEntry> entries;
        XBOX_TRY_ASSIGN(entries, list_installed_for_title(tid));
        out.insert(out.end(), entries.begin(), entries.end());
    }
    return out;
}

Result<PathResolver::InstalledEntry, Error> PathResolver::find_installed(
    u32 title_id, std::string_view file_name_or_prefix) const {
    std::vector<InstalledEntry> entries;
    XBOX_TRY_ASSIGN(entries, list_installed_for_title(title_id));
    if (entries.empty()) {
        return XBOX_INSTALL_ERROR(ContentNotInstalled,
            "no installed content for title " + path::format_title_id(title_id));
    }

    // Find by file_name (case-insensitive) - exact match first, then prefix
    std::vector<const InstalledEntry*> exact_matches;
    std::vector<const InstalledEntry*> prefix_matches;

    std::string query_lower = str::to_lower(std::string(file_name_or_prefix));
    for (const auto& e : entries) {
        std::string fn_lower = str::to_lower(e.file_name);
        if (fn_lower == query_lower) {
            exact_matches.push_back(&e);
        } else if (str::starts_with(fn_lower, query_lower)) {
            prefix_matches.push_back(&e);
        }
    }

    std::vector<const InstalledEntry*>* matches = &exact_matches;
    if (exact_matches.empty()) matches = &prefix_matches;

    if (matches->empty()) {
        return XBOX_INSTALL_ERROR(ContentNotInstalled,
            "no installed content for title " + path::format_title_id(title_id) +
            " with name/prefix '" + std::string(file_name_or_prefix) + "'");
    }
    if (matches->size() > 1) {
        return XBOX_INSTALL_ERROR(ConflictResolutionFailed,
            "ambiguous file name '" + std::string(file_name_or_prefix) +
            "' matches " + std::to_string(matches->size()) + " packages");
    }
    return *matches->at(0);
}

Result<PathResolver::InstalledEntry, Error> PathResolver::find_installed(
    u32 title_id, std::string_view file_name_or_prefix,
    u64 xuid_filter) const {
    std::vector<InstalledEntry> entries;
    XBOX_TRY_ASSIGN(entries, list_installed_for_title(title_id));
    if (entries.empty()) {
        return XBOX_INSTALL_ERROR(ContentNotInstalled,
            "no installed content for title " + path::format_title_id(title_id));
    }

    // Format the xuid_filter as a 16-char uppercase hex string for comparison
    char xuid_buf[32];
    std::snprintf(xuid_buf, sizeof(xuid_buf), "%016llX",
                  static_cast<unsigned long long>(xuid_filter));

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

    // Find by file_name (case-insensitive) - exact match first, then prefix
    // Filter by xuid: only include entries whose xuid matches the filter,
    // OR entries that are shared content (TU/DLC) under xuid=0.
    std::vector<const InstalledEntry*> exact_matches;
    std::vector<const InstalledEntry*> prefix_matches;

    std::string query_lower = str::to_lower(std::string(file_name_or_prefix));
    for (const auto& e : entries) {
        // XUID filter: skip entries that don't match
        // Shared content (TU/DLC) is always included regardless of xuid
        if (e.xuid != xuid_filter && !is_shared_content(e.content_type)) {
            continue;
        }

        std::string fn_lower = str::to_lower(e.file_name);
        if (fn_lower == query_lower) {
            exact_matches.push_back(&e);
        } else if (str::starts_with(fn_lower, query_lower)) {
            prefix_matches.push_back(&e);
        }
    }

    std::vector<const InstalledEntry*>* matches = &exact_matches;
    if (exact_matches.empty()) matches = &prefix_matches;

    if (matches->empty()) {
        return XBOX_INSTALL_ERROR(ContentNotInstalled,
            "no installed content for title " + path::format_title_id(title_id) +
            " with name/prefix '" + std::string(file_name_or_prefix) + "'" +
            " (xuid filter: " + std::string(xuid_buf) + ")");
    }
    if (matches->size() > 1) {
        // List the matching entries to help the user disambiguate
        std::string detail;
        for (std::size_t i = 0; i < matches->size() && i < 5; ++i) {
            const auto* m = matches->at(i);
            if (i > 0) detail += ", ";
            detail += "xuid=" + format_xuid(m->xuid) + " ct=" +
                      str::format_hex_u32(m->content_type) + " name=" + m->file_name;
        }
        return XBOX_INSTALL_ERROR(ConflictResolutionFailed,
            "ambiguous file name '" + std::string(file_name_or_prefix) +
            "' matches " + std::to_string(matches->size()) + " packages: " + detail);
    }
    return *matches->at(0);
}

fs::path PathResolver::disabled_path(const InstallLocation& loc) const {
    return fs::path(std::string(loc.content_id_dir.string()) + paths::DISABLED_SUFFIX);
}

fs::path PathResolver::header_path(const InstallLocation& loc) const {
    // Per Xenia's content_manager.cc: ResolvePackageHeaderPath
    // content_root/<xuid>/<title_id>/Headers/<content_type>/<file_name>.header
    return loc.title_dir / "Headers" /
           path::format_content_type(loc.content_type) /
           (loc.file_name + ".header");
}

fs::path PathResolver::disabled_path_for(const InstalledEntry& e) const {
    if (e.is_disabled) return e.path;
    return fs::path(std::string(e.path.string()) + paths::DISABLED_SUFFIX);
}

bool PathResolver::is_disabled(const InstalledEntry& e) noexcept {
    return e.is_disabled;
}

} // namespace xbox::installer
