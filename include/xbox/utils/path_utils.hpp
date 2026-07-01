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

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xbox::path {

namespace fs = std::filesystem;

// Safe path join - rejects path-traversal components (../, ..\).
// Returns nullopt if `child` would escape `parent`.
[[nodiscard]] std::optional<fs::path> safe_join(const fs::path& parent, std::string_view child);

// Same as safe_join but takes a sequence of child segments.
[[nodiscard]] std::optional<fs::path> safe_join_many(const fs::path& parent,
    const std::vector<std::string>& children);

// Ensure a directory exists (creates recursively if missing).
// Returns the directory's path on success.
[[nodiscard]] ResultT<fs::path> ensure_directory(const fs::path& p);

// Atomically move `from` to `to`, replacing `to` if it exists.
// On Windows, falls back to "rename via temp" if target exists (since
// MoveFileEx with MOVEFILE_REPLACE_EXISTING is needed).
[[nodiscard]] Result<void, Error> atomic_replace(const fs::path& from, const fs::path& to);

// Write data to a temp file in the same directory as `final`, then atomically
// rename it to `final`. This prevents partial writes from corrupting an
// existing file (write-then-rename pattern).
[[nodiscard]] Result<void, Error> atomic_write_file(const fs::path& final_path,
    const void* data, std::size_t size);

// Recursively remove a directory tree, with explicit error propagation
// for each failure (does NOT abort on first error - reports all).
struct RemovalReport {
    std::vector<std::string> failed_paths;
    std::size_t removed_count{0};
};
[[nodiscard]] Result<RemovalReport, Error> recursive_remove_all(const fs::path& p);

// File size query - returns std::nullopt if file doesn't exist or isn't regular.
[[nodiscard]] std::optional<u64> file_size(const fs::path& p) noexcept;

// Convert a path to a display string using forward slashes (for cross-platform logging).
[[nodiscard]] std::string display_path(const fs::path& p);

// Format a Title ID as 8-character uppercase hex (e.g. 0x415607ED -> "415607ED").
[[nodiscard]] std::string format_title_id(u32 title_id);

// Format a content type as 8-character uppercase hex.
[[nodiscard]] std::string format_content_type(u32 content_type);

// Format a 20-byte content ID (binary) as 40-character lowercase hex.
[[nodiscard]] std::string format_content_id(const u8* id, std::size_t len);

// Check whether the given path is writable (creates and removes a temp file).
[[nodiscard]] bool is_writable(const fs::path& dir) noexcept;

// Resolve a path that may be relative to the user's home directory (~ on Unix,
// %USERPROFILE% on Windows). Does not expand other env vars.
[[nodiscard]] fs::path expand_home(std::string_view p);

} // namespace xbox::path
