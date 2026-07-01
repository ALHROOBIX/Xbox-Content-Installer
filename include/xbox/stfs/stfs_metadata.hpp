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

#include <span>
#include <string>
#include <string_view>

namespace xbox::stfs {

// Quick checks that operate on the first few bytes only.
[[nodiscard]] bool is_stfs_magic(const void* first_4_bytes) noexcept;

// Quick read of the Title ID field at offset 0x360 (4 bytes big-endian).
// Requires `buffer` to be at least 0x364 bytes.
[[nodiscard]] Result<u32, Error> quick_read_title_id(std::span<const byte> buffer);

// Quick read of the content type at offset 0x344 (4 bytes).
[[nodiscard]] Result<u32, Error> quick_read_content_type(std::span<const byte> buffer);

// Quick read of the 20-byte content ID at offset 0x32C.
[[nodiscard]] Result<std::array<u8, 20>, Error> quick_read_content_id(std::span<const byte> buffer);

// Quick read of the metadata version (1 or 2) at offset 0x348.
[[nodiscard]] Result<u32, Error> quick_read_metadata_version(std::span<const byte> buffer);

// Read the first non-empty display name (UTF-16 BE -> UTF-8).
// Reads 12 locale entries (each 0x80 bytes = 64 UTF-16 chars), returns
// the first one whose first char is non-null.
[[nodiscard]] Result<std::string, Error> quick_read_display_name(std::span<const byte> buffer);

// Identify the content type as a human-readable category:
//   "Title Update"  for content_type == 0x000B0000
//   "DLC"           for content_type == 0x00000002
//   "Saved Game"    for content_type == 0x00000001
//   "Theme"         for content_type == 0x00030000
//   ... etc
[[nodiscard]] std::string classify_content_type(u32 content_type) noexcept;

// True if the content type corresponds to a title update (TU).
[[nodiscard]] inline bool is_title_update(u32 content_type) noexcept {
    return content_type == 0x000B0000;
}

// True if the content type is DLC / marketplace content.
[[nodiscard]] inline bool is_dlc(u32 content_type) noexcept {
    return content_type == 0x00000002;
}

} // namespace xbox::stfs
