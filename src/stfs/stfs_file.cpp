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
#include "xbox/stfs/stfs_file.hpp"

#include "xbox/core/endian.hpp"
#include "xbox/core/errors.hpp"

#include <cstring>

namespace xbox::stfs {

Result<FileEntry, Error> parse_file_entry(const void* ptr) {
    const auto* p = static_cast<const u8*>(ptr);

    // Check end-of-list marker: all-zero name + zero flags
    bool all_zero = true;
    for (std::size_t i = 0; i <= 0x28; ++i) {
        if (p[i] != 0) { all_zero = false; break; }
    }
    if (all_zero) {
        return XBOX_STFS_ERROR(InvalidFileEntry, "end-of-list marker");
    }

    FileEntry e{};

    // File name: 0x28 (40) bytes, null-padded ASCII
    {
        std::size_t name_len = 0;
        while (name_len < 0x28 && p[name_len] != 0) ++name_len;
        e.name.assign(reinterpret_cast<const char*>(p), name_len);
    }

    // Flags byte at 0x28
    e.flags = p[0x28];

    // If name length in flags is non-zero, truncate to that
    u8 name_len_field = e.flags & FILE_NAME_LENGTH_MASK;
    if (name_len_field > 0 && name_len_field <= e.name.size()) {
        e.name.resize(name_len_field);
    }

    // Blocks allocated (24-bit LE) at 0x29
    e.blocks_allocated = static_cast<u32>(p[0x29]) |
                         (static_cast<u32>(p[0x2A]) << 8) |
                         (static_cast<u32>(p[0x2B]) << 16);

    // Copy of 0x29 at 0x2C - we ignore (just informational)

    // Starting block number (24-bit LE) at 0x2F
    e.starting_block_number = static_cast<u32>(p[0x2F]) |
                              (static_cast<u32>(p[0x30]) << 8) |
                              (static_cast<u32>(p[0x31]) << 16);

    // Path indicator (16-bit BIG-ENDIAN) at 0x32
    u16 pi = endian::read_u16_be(p + 0x32);
    e.path_indicator = static_cast<i16>(pi);

    // File size (32-bit BIG-ENDIAN) at 0x34
    e.file_size = endian::read_u32_be(p + 0x34);

    // Update timestamp (32-bit) at 0x38
    e.update_timestamp = endian::read_u32_be(p + 0x38);

    // Access timestamp (32-bit) at 0x3C
    e.access_timestamp = endian::read_u32_be(p + 0x3C);

    return e;
}

Result<std::vector<FileEntry>, Error> parse_file_table(std::span<const byte> table_data) {
    if (table_data.size() % stfs::FILE_ENTRY_SIZE != 0) {
        // Allow trailing padding but warn
    }

    std::vector<FileEntry> entries;
    entries.reserve(64);

    std::size_t entry_count = table_data.size() / stfs::FILE_ENTRY_SIZE;
    for (std::size_t i = 0; i < entry_count; ++i) {
        const auto* p = table_data.data() + i * stfs::FILE_ENTRY_SIZE;
        auto r = parse_file_entry(p);
        if (!r.is_ok()) {
            // End-of-list marker reached
            break;
        }
        entries.push_back(std::move(r).value());
    }
    return entries;
}

std::vector<FileNode> build_file_tree(const std::vector<FileEntry>& entries) {
    std::vector<FileNode> nodes;
    nodes.reserve(entries.size());
    for (const auto& e : entries) {
        nodes.push_back({e, {}});
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        i16 pi = entries[i].path_indicator;
        if (pi == -1 || pi == 0xFFFF) continue;
        // pi is an index into the entries array (the parent directory)
        if (static_cast<std::size_t>(pi) >= entries.size()) continue;
        nodes[static_cast<std::size_t>(pi)].child_indices.push_back(i);
    }
    return nodes;
}

std::string FileEntry::format() const {
    std::string out;
    out.reserve(128);
    out.append(is_directory() ? "[DIR] " : "[FILE] ");
    out.append(name);
    if (!is_directory()) {
        out.append(" (").append(std::to_string(file_size)).append(" bytes, ");
        out.append(std::to_string(blocks_allocated)).append(" blocks, start=");
        out.append(std::to_string(starting_block_number)).append(")");
    }
    out.append(" parent=");
    if (path_indicator == -1) out.append("<root>");
    else out.append(std::to_string(path_indicator));
    return out;
}

} // namespace xbox::stfs
