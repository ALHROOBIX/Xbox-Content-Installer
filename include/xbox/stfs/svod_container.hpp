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

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xbox::stfs {

namespace fs = std::filesystem;

// SVOD device descriptor (0x24 bytes at offset 0x37A in the header)
// Different from STFS volume descriptor - same offset, different fields.
struct SvodDeviceDescriptor {
    u8  descriptor_length{0};            // +0x00
    u8  block_cache_element_count{0};    // +0x01
    u8  worker_thread_processor{0};      // +0x02
    u8  worker_thread_priority{0};       // +0x03
    std::array<u8, 0x14> first_fragment_hash_entry{}; // +0x04 (20 bytes SHA1)
    u8  features_byte{0};                // +0x18 - bit 6 = enhanced_gdf_layout (per Xenia)
    u32 num_data_blocks{0};              // +0x19 (3 bytes LE)
    u32 start_data_block{0};             // +0x1C (3 bytes LE)
    std::array<u8, 5> reserved{};        // +0x1F

    // True if this SVOD uses EnhancedGDF layout.
    // Per Xenia's stfs_xbox.h SvodDeviceDescriptor:
    //   union {
    //     struct {
    //       uint8_t must_be_zero_for_future_usage : 6;  // bits 0-5
    //       uint8_t enhanced_gdf_layout : 1;            // bit 6  (value 0x40)
    //       uint8_t zero_for_downlevel_clients : 1;     // bit 7  (value 0x80)
    //     } bits;
    //   } features;
    // So enhanced_gdf_layout is BIT 6 (0x40), NOT bit 1 (0x02).
    [[nodiscard]] bool is_enhanced_gdf() const noexcept {
        return (features_byte & 0x40) != 0;
    }

    [[nodiscard]] std::string to_string() const;
};

// Parse the SVOD device descriptor from a 0x24-byte buffer.
[[nodiscard]] Result<SvodDeviceDescriptor, Error> parse_svod_descriptor(
    std::span<const byte> buffer);

// Parse the SVOD descriptor from a full STFS header buffer.
[[nodiscard]] Result<SvodDeviceDescriptor, Error> parse_svod_descriptor_from_header(
    std::span<const byte> header_buffer);

// Detect the SVOD layout type by examining the data files.
// `header_path` is the path to the main header file (e.g. "package.stfs").
// We look for a `.data` directory next to it and examine the first fragment.
[[nodiscard]] Result<SvodLayoutType, Error> detect_svod_layout(
    const fs::path& header_path);

// Convert SVOD block index to (file_index, byte_offset_within_file).
struct SvodBlockLocation {
    std::size_t file_index{0};   // which Data000X fragment
    u64 byte_offset{0};          // offset within that fragment
};

// Compute the location of a SVOD block in the multi-file layout.
// `start_data_block` comes from the SVOD device descriptor.
[[nodiscard]] SvodBlockLocation svod_block_to_location(
    u32 block_index,
    u32 start_data_block,
    SvodLayoutType layout,
    u64 svod_base_offset = 0) noexcept;

// SVOD directory entry (binary tree node, 0xE bytes)
struct SvodDirectoryEntry {
    u16 node_l{0};           // left child node index
    u16 node_r{0};           // right child node index
    u32 data_block{0};       // starting data block (sector)
    u32 length{0};           // file size in bytes
    u8  attributes{0};       // file attributes
    u8  name_length{0};      // length of name
    std::string name;        // decoded name (variable length)

    [[nodiscard]] bool is_directory() const noexcept {
        return (attributes & 0x10) != 0;
    }

    [[nodiscard]] std::string format() const;
};

// Parse a single SVOD directory entry from a 0xE-byte buffer + name bytes.
// Returns the entry and the number of bytes consumed (0xE + name_length).
[[nodiscard]] Result<std::pair<SvodDirectoryEntry, std::size_t>, Error>
parse_svod_directory_entry(const u8* data, std::size_t max_len);

// List the data fragment files for a SVOD package.
// Returns sorted paths (Data0000, Data0001, ...).
[[nodiscard]] Result<std::vector<fs::path>, Error> list_svod_data_files(
    const fs::path& header_path);

// SVOD package information
struct SvodPackageInfo {
    SvodDeviceDescriptor descriptor{};
    SvodLayoutType layout{SvodLayoutType::Unknown};
    std::vector<fs::path> data_files;       // sorted fragment paths
    u64 total_data_size{0};                 // combined size of all fragments
    u64 svod_base_offset{0};                // magic offset within first fragment
};

// Read all SVOD metadata from a header file.
[[nodiscard]] Result<SvodPackageInfo, Error> read_svod_info(
    const fs::path& header_path);

// Get human-readable name for an SVOD layout type
[[nodiscard]] inline std::string layout_name(SvodLayoutType layout) {
    switch (layout) {
        case SvodLayoutType::EnhancedGDF:   return "EnhancedGDF (EGDF)";
        case SvodLayoutType::XSF:           return "XSF";
        case SvodLayoutType::SingleFile:    return "SingleFile";
        case SvodLayoutType::MultipleFiles: return "MultipleFiles";
        default:                            return "Unknown";
    }
}

} // namespace xbox::stfs
