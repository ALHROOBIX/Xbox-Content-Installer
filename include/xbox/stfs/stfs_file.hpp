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

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace xbox::stfs {

// Bit mask for the "is directory" flag in byte 0x28 of file entries.
// Per Xenia's stfs_xbox.h and verified against real Xbox 360 files:
// bit 7 (0x80) = directory, bits 0-5 = name length, bit 6 = unknown/unused
constexpr u8 FILE_FLAG_DIRECTORY = 0x80;
// Mask for the name-length portion (low 6 bits) of byte 0x28.
constexpr u8 FILE_NAME_LENGTH_MASK = 0x3F;

// Single file/directory entry (0x40 bytes when serialized).
struct FileEntry {
    std::string name;                  // decoded file name (max 40 chars)
    u8  flags{0};                       // raw byte at offset 0x28
    u32 blocks_allocated{0};           // 24-bit value
    u32 starting_block_number{0};      // 24-bit value
    i16 path_indicator{-1};             // -1 (0xFFFF) = root
    u32 file_size{0};                   // bytes; 0 for directories
    u32 update_timestamp{0};
    u32 access_timestamp{0};

    [[nodiscard]] bool is_directory() const noexcept {
        return (flags & FILE_FLAG_DIRECTORY) != 0;
    }

    [[nodiscard]] u8 name_length() const noexcept {
        return flags & FILE_NAME_LENGTH_MASK;
    }

    [[nodiscard]] std::string format() const;
};

// Parse a single 0x40-byte entry from a buffer.
[[nodiscard]] Result<FileEntry, Error> parse_file_entry(const void* ptr);

// Parse an entire file-listing table (multiple 0x1000-byte blocks).
// `table_data` must be at least file_table_block_count * 0x1000 bytes.
// Stops at the first entry whose name length is 0 (end-of-list marker).
[[nodiscard]] Result<std::vector<FileEntry>, Error> parse_file_table(
    std::span<const byte> table_data);

// Build a tree of file entries (rooted at path_indicator == 0xFFFF).
struct FileNode {
    FileEntry entry;
    std::vector<std::size_t> child_indices;  // indices into the flat list
};

[[nodiscard]] std::vector<FileNode> build_file_tree(
    const std::vector<FileEntry>& entries);

} // namespace xbox::stfs
