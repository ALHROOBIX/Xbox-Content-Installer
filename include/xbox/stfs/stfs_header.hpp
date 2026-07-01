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
#include <string_view>

namespace xbox::stfs {

// STFS package type, determined by magic at offset 0.
enum class PackageType : u8 {
    Unknown = 0,
    CON,    // Console-signed (created by Xbox 360 itself)
    PIRS,   // Microsoft-signed (system updates, etc)
    LIVE,   // Microsoft-signed (downloaded content - DLC, TU, etc)
};

[[nodiscard]] std::string_view package_type_name(PackageType t) noexcept;

// License entry (16 bytes) - read from offset 0x22C, 16 entries total.
// Per Xenia's stfs_xbox.h: XContentLicense
struct LicenseEntry {
    u64 licensee_id{0};    // +0x00 (8 bytes BE)
    u32 license_bits{0};   // +0x08 (4 bytes BE)
    u32 license_flags{0};  // +0x0C (4 bytes BE)

    // True if this license is "active" (license_flags != 0)
    [[nodiscard]] bool is_active() const noexcept { return license_flags != 0; }

    [[nodiscard]] std::string format() const;
};

// Compute the OR of all active license_bits (what Xenia calls license_mask)
[[nodiscard]] u32 compute_license_mask(const std::array<LicenseEntry, license::ENTRY_COUNT>& licenses) noexcept;

// The 0x24-byte STFS Volume Descriptor.
// Per Free60 STFS spec, verified against real Xbox 360 files.
struct VolumeDescriptor {
    u8  reserved{0};                    // +0x00
    u8  block_separation{0};            // +0x01 (non-zero = two hash tables per group)
    u16 file_table_block_count{0};      // +0x02 (2 bytes LE!)
    u32 file_table_block_number{0};     // +0x04 (3 bytes LE, stored as u32)
    std::array<u8, 0x14> top_hash_table_hash{}; // +0x07
    i32 total_allocated_blocks{0};      // +0x1B (4 bytes BE)
    i32 total_unallocated_blocks{0};    // +0x1F (4 bytes BE)

    // True if the package uses two hash tables per hash-table block
    [[nodiscard]] bool has_two_hash_tables() const noexcept {
        return block_separation != 0;
    }

    [[nodiscard]] std::string to_string() const;
};

// Parsed STFS metadata (excluding the raw signature/certificate bytes,
// which we keep as raw spans for verification).
struct StfsHeader {
    PackageType type{PackageType::Unknown};
    std::array<u8, 4> magic{};

    // Package signature (LIVE/PIRS) - 0x228 bytes at offset 0x04
    // Per Xenia's stfs_xbox.h: signature[0x228] (NOT verified by Xenia)
    std::span<const byte> package_signature{};   // owned by the source buffer

    // License entries (16 × 16 bytes = 0x100 bytes) at offset 0x22C
    // Per Xenia's stfs_xbox.h: XContentLicense licenses[license_count]
    std::array<LicenseEntry, license::ENTRY_COUNT> licenses{};

    // Content ID (20 bytes) at offset 0x32C
    std::array<u8, 20> content_id{};

    // Header size (4 bytes BE) at offset 0x340 - typically 0x971A
    u32 header_size{0};

    // Entry ID (4 bytes) at offset 0x340 - controls hash-table layout!
    // (Note: header_size IS the entry_id field per Xenia - they're at the same offset)
    // Actually per Xenia: header_size is at 0x340, and it's the "header_size" field.
    // The "entry_id" / "two hash tables" detection uses a different field.
    // Let's keep both names for clarity.
    u32 entry_id{0};

    // Content type (4 bytes signed) at offset 0x344
    u32 content_type{0};

    // Metadata version (1 or 2) at offset 0x348
    u32 metadata_version{1};

    // Content size (8 bytes signed) at offset 0x34C
    u64 content_size{0};

    // Media ID at 0x354
    u32 media_id{0};

    // Version & base version (for title updates) at 0x358 / 0x35C
    u32 version{0};
    u32 base_version{0};

    // *** TITLE ID *** - 4 bytes BIG-ENDIAN at offset 0x360
    u32 title_id{0};

    // Platform / Exec Type / Disc info at 0x364..0x367
    u8  platform{0};
    u8  exec_type{0};
    u8  disc_number{0};
    u8  disc_in_set{0};

    // Save Game ID at 0x368
    u32 save_game_id{0};

    // Console ID (5 bytes) at 0x36C
    std::array<u8, 5> console_id{};

    // Profile ID (8 bytes) at 0x371 - used as xuid by Xenia for path!
    std::array<u8, 8> profile_id{};

    // Volume descriptor (0x24 bytes) at 0x37A
    VolumeDescriptor volume{};

    // Data file count & combined size
    i32 data_file_count{0};
    u64 data_file_combined_size{0};

    // *** Volume Type *** (4 bytes BE) at offset 0x3A9
    // 0 = STFS, 1 = SVOD (per Xenia's stfs_xbox.h: XContentVolumeType)
    VolumeType volume_type{VolumeType::Stfs};

    // Online creator (8 bytes BE) at 0x3AD
    u64 online_creator{0};

    // Category (4 bytes BE) at 0x3B5
    u32 category{0};

    // Device ID (20 bytes) at 0x3FD
    std::array<u8, 20> device_id{};

    // Display name & description (12 locales each, UTF-16 BE)
    // We decode only the first non-empty locale (typically English) and
    // also store the raw bytes for reference.
    std::string display_name{};          // decoded UTF-8
    std::string display_description{};   // decoded UTF-8
    std::string publisher_name{};        // UTF-8 (at 0x1611)
    std::string title_name{};            // UTF-8 (at 0x1691)

    // Thumbnail image sizes (bytes)
    i32 thumbnail_size{0};
    i32 title_thumbnail_size{0};

    // Whether the package uses two hash tables per 170-block group.
    // Per Xenia: CON (writable) uses 2 hash tables, LIVE/PIRS (readonly) use 1
    [[nodiscard]] bool uses_two_hash_tables() const noexcept {
        return type == PackageType::CON;  // CON = writable = 2 hash tables
    }

    // blocks_per_hash_table: 1 for readonly (LIVE/PIRS), 2 for writable (CON)
    // Per Xenia's SetupContainer: blocks_per_hash_table_ = is_package_readonly() ? 1 : 2
    [[nodiscard]] u32 blocks_per_hash_table() const noexcept {
        // LIVE and PIRS are readonly (bph=1), CON is writable (bph=2)
        // Per Xenia: is_package_readonly() checks if magic != CON
        return (type == PackageType::CON) ? 2 : 1;
    }

    // True if this is a CON package (locally-signed by the console)
    [[nodiscard]] bool is_con() const noexcept { return type == PackageType::CON; }
    // True if this is a LIVE or PIRS package (Microsoft-signed)
    [[nodiscard]] bool is_ms_signed() const noexcept {
        return type == PackageType::LIVE || type == PackageType::PIRS;
    }

    // True if this is a SVOD container (multi-file, large DLC/GOD)
    [[nodiscard]] bool is_svod() const noexcept { return volume_type == VolumeType::Svod; }

    // Pretty-printed summary for `info` command
    [[nodiscard]] std::string format_summary() const;

    // Human-readable content type name
    [[nodiscard]] std::string content_type_name() const;

    // License mask (OR of all active license_bits, per Xenia's license_mask())
    [[nodiscard]] u32 license_mask() const noexcept {
        return compute_license_mask(licenses);
    }
};

// Parse the STFS header from a buffer of at least stfs::HEADER_SIZE bytes.
// `buffer` must outlive the returned StfsHeader (it stores spans into it).
[[nodiscard]] Result<StfsHeader, Error> parse_header(std::span<const byte> buffer);

// Convenience: parse just the magic to identify the package type.
[[nodiscard]] PackageType identify_package_type(const void* first_4_bytes) noexcept;

} // namespace xbox::stfs
