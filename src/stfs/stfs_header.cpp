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
#include "xbox/stfs/stfs_header.hpp"
#include "xbox/stfs/stfs_metadata.hpp"
#include "xbox/stfs/stfs_volume.hpp"

#include "xbox/core/endian.hpp"
#include "xbox/core/errors.hpp"
#include "xbox/core/logger.hpp"
#include "xbox/utils/string_utils.hpp"

namespace xbox::stfs {

std::string_view package_type_name(PackageType t) noexcept {
    switch (t) {
        case PackageType::CON:     return "CON";
        case PackageType::PIRS:    return "PIRS";
        case PackageType::LIVE:    return "LIVE";
        case PackageType::Unknown: return "Unknown";
    }
    return "Unknown";
}

std::string LicenseEntry::format() const {
    char buf[128];
    std::snprintf(buf, sizeof(buf),
        "licensee_id=%016llX bits=%08X flags=%08X%s",
        static_cast<unsigned long long>(licensee_id),
        license_bits, license_flags,
        is_active() ? " (active)" : "");
    return buf;
}

u32 compute_license_mask(const std::array<LicenseEntry, license::ENTRY_COUNT>& licenses) noexcept {
    // Per Xenia's xcontent_container_device.h: license_mask()
    // OR all license_bits where license_flags != 0
    u32 mask = 0;
    for (const auto& lic : licenses) {
        if (lic.license_flags != 0) {
            mask |= lic.license_bits;
        }
    }
    return mask;
}

// uses_two_hash_tables() is now inline in the header (based on package type)

std::string StfsHeader::content_type_name() const {
    return classify_content_type(content_type);
}

std::string StfsHeader::format_summary() const {
    std::string out;
    out.reserve(1024);
    out.append("Package Type:    ").append(std::string(package_type_name(type))).append("\n");
    out.append("Volume Type:     ");
    switch (volume_type) {
        case VolumeType::Stfs:    out.append("STFS (single-file container)\n"); break;
        case VolumeType::Svod:    out.append("SVOD (multi-file container)\n"); break;
        default:                  out.append("Unknown\n"); break;
    }
    out.append("Title ID:        ").append(str::format_hex_u32(title_id)).append("\n");
    out.append("Content Type:    ")
       .append(str::format_hex_u32(content_type))
       .append(" (")
       .append(content_type_name())
       .append(")\n");
    out.append("Content ID:      ").append(str::to_hex(content_id.data(), content_id.size())).append("\n");
    out.append("Header Size:     ").append(str::format_hex_u32(header_size)).append(" (").append(std::to_string(header_size)).append(" bytes)\n");
    out.append("Metadata Ver:    ").append(std::to_string(metadata_version)).append("\n");
    out.append("Content Size:    ").append(std::to_string(content_size)).append(" bytes\n");
    out.append("Version:         ").append(std::to_string(version)).append("\n");
    out.append("Base Version:    ").append(std::to_string(base_version)).append("\n");
    out.append("Media ID:        ").append(str::format_hex_u32(media_id)).append("\n");
    out.append("Category:        ").append(str::format_hex_u32(category)).append("\n");
    out.append("Display Name:    ").append(display_name).append("\n");
    out.append("Description:     ").append(display_description).append("\n");
    out.append("Publisher:       ").append(publisher_name).append("\n");
    out.append("Title Name:      ").append(title_name).append("\n");
    out.append("Platform:        ").append(std::to_string(platform)).append("\n");
    out.append("Exec Type:       ").append(std::to_string(exec_type)).append("\n");
    out.append("Disc:            ").append(std::to_string(disc_number)).append(" of ")
       .append(std::to_string(disc_in_set)).append("\n");
    out.append("Profile ID:      ").append(str::to_hex(profile_id.data(), profile_id.size())).append("\n");
    out.append("Console ID:      ").append(str::to_hex(console_id.data(), console_id.size())).append("\n");
    out.append("Device ID:       ").append(str::to_hex(device_id.data(), device_id.size())).append("\n");
    out.append("Two Hash Tables: ").append(uses_two_hash_tables() ? "yes" : "no").append("\n");

    // Save Game specific info
    if (content_type == content_type::SAVED_GAME) {
        out.append("Save Game ID:    ").append(str::format_hex_u32(save_game_id)).append("\n");
    }

    // License info
    u32 lmask = license_mask();
    out.append("License Mask:    ").append(str::format_hex_u32(lmask)).append("\n");
    int active_count = 0;
    for (const auto& lic : licenses) {
        if (lic.is_active()) ++active_count;
    }
    out.append("Active Licenses: ").append(std::to_string(active_count)).append(" / 16\n");

    // SVOD-specific info
    if (is_svod()) {
        out.append("Data Files:      ").append(std::to_string(data_file_count)).append("\n");
        out.append("Data Combined:   ").append(std::to_string(data_file_combined_size)).append(" bytes\n");
    }

    out.append("Vol Descriptor:\n").append(volume.to_string());
    return out;
}

std::string VolumeDescriptor::to_string() const {
    std::string out;
    out.reserve(256);
    out.append("  Block Separation:       ").append(std::to_string(block_separation));
    out.append(has_two_hash_tables() ? " (two hash tables)\n" : " (single hash table)\n");
    out.append("  File Table Block Count: ").append(std::to_string(file_table_block_count)).append("\n");
    out.append("  File Table Block Num:   ").append(std::to_string(file_table_block_number)).append("\n");
    out.append("  Top Hash Table Hash:    ")
       .append(str::to_hex(top_hash_table_hash.data(), top_hash_table_hash.size())).append("\n");
    out.append("  Total Allocated:        ").append(std::to_string(total_allocated_blocks)).append(" blocks\n");
    out.append("  Total Unallocated:      ").append(std::to_string(total_unallocated_blocks)).append(" blocks\n");
    return out;
}

PackageType identify_package_type(const void* first_4_bytes) noexcept {
    const auto* p = static_cast<const u8*>(first_4_bytes);
    if (p[0] == 'C' && p[1] == 'O' && p[2] == 'N' && p[3] == ' ') return PackageType::CON;
    if (p[0] == 'P' && p[1] == 'I' && p[2] == 'R' && p[3] == 'S') return PackageType::PIRS;
    if (p[0] == 'L' && p[1] == 'I' && p[2] == 'V' && p[3] == 'E') return PackageType::LIVE;
    return PackageType::Unknown;
}

Result<StfsHeader, Error> parse_header(std::span<const byte> buffer) {
    if (buffer.size() < static_cast<std::size_t>(stfs::HEADER_SIZE)) {
        return XBOX_STFS_ERROR(InvalidHeaderSize,
            "buffer too small for STFS header: " + std::to_string(buffer.size()) +
            " < " + std::to_string(stfs::HEADER_SIZE));
    }

    // Use u8 pointer for byte-level access (more convenient than std::byte here)
    const auto* p = reinterpret_cast<const u8*>(buffer.data());

    StfsHeader h{};
    std::memcpy(h.magic.data(), p + stfs::off::MAGIC, 4);
    h.type = identify_package_type(p);
    if (h.type == PackageType::Unknown) {
        return XBOX_STFS_ERROR(InvalidMagic,
            "unrecognized STFS magic: " + str::to_hex(h.magic.data(), 4));
    }

    // Package signature (LIVE/PIRS) - we don't verify it (Microsoft RSA keys
    // are not available), but we record the span for reference.
    // Per Xenia's stfs_xbox.h: signature is 0x228 bytes at offset 0x004
    if (h.type != PackageType::CON) {
        h.package_signature = std::span<const byte>(
            reinterpret_cast<const byte*>(p + stfs::off::PACKAGE_SIGNATURE), 0x228);
    } else {
        // CON files have a different signature layout (console certificate + RSA sig)
        // but for parsing purposes we record the same span
        h.package_signature = std::span<const byte>(
            reinterpret_cast<const byte*>(p + stfs::off::PACKAGE_SIGNATURE), 0x228);
    }

    // License entries (16 × 16 bytes) at offset 0x22C
    // Per Xenia's stfs_xbox.h: XContentLicense licenses[license_count]
    for (std::size_t i = 0; i < license::ENTRY_COUNT; ++i) {
        const u8* lp = p + stfs::off::LICENSE_ENTRIES + i * license::ENTRY_SIZE;
        h.licenses[i].licensee_id  = endian::read_u64_be(lp + license::LICENSEE_ID_OFFSET);
        h.licenses[i].license_bits = endian::read_u32_be(lp + license::LICENSE_BITS_OFFSET);
        h.licenses[i].license_flags = endian::read_u32_be(lp + license::LICENSE_FLAGS_OFFSET);
    }

    // Content ID (20 bytes) at 0x32C
    std::memcpy(h.content_id.data(), p + stfs::off::CONTENT_ID, 20);

    // Header size (4 bytes BE) at offset 0x340 - typically 0x971A
    // Per Xenia's stfs_xbox.h: header_size field
    h.header_size = endian::read_u32_be(p + stfs::HEADER_SIZE_FIELD);

    // Entry ID (4 bytes) at 0x340 - BIG-ENDIAN (same field as header_size)
    // Per the STFS spec, Entry ID controls the two-hash-tables layout:
    //   ((entry_id + 0xFFF) & 0xF000) >> 0xC == 0xB  →  two hash tables per group
    // Real Xbox 360 files (e.g. Minecraft TU) have entry_id = 0xAD0E which
    // triggers two-hash-tables mode. Reading as LE gives 0x0EAD0000 which
    // does NOT match, so this MUST be read as BE.
    h.entry_id = endian::read_u32_be(p + stfs::off::ENTRY_ID);

    // Content type (4 bytes) at 0x344 - BIG-ENDIAN
    h.content_type = endian::read_u32_be(p + stfs::off::CONTENT_TYPE);

    // Metadata version (4 bytes) at 0x348 - BIG-ENDIAN
    // Per spec: 1 or 2. However, real-world Xbox 360 files (e.g. Minecraft TU)
    // sometimes have metadata_version = 0, which we treat as v1 (no v2 fields).
    // Xenia doesn't strictly validate this field, so we accept any value.
    h.metadata_version = endian::read_u32_be(p + stfs::off::METADATA_VERSION);
    if (h.metadata_version > 2) {
        XBOX_LOG_WARN("STFS metadata version {} is unusual (expected 0, 1, or 2) - treating as v2",
                       h.metadata_version);
    }

    // Content size (8 bytes) at 0x34C - BIG-ENDIAN
    h.content_size = endian::read_u64_be(p + stfs::off::CONTENT_SIZE);

    // Media ID at 0x354 - BIG-ENDIAN
    h.media_id = endian::read_u32_be(p + stfs::off::MEDIA_ID);

    // Version / Base Version at 0x358 / 0x35C - BIG-ENDIAN
    h.version = endian::read_u32_be(p + stfs::off::VERSION);
    h.base_version = endian::read_u32_be(p + stfs::off::BASE_VERSION);

    // *** TITLE ID *** at 0x360 - 4 bytes BIG-ENDIAN
    h.title_id = endian::read_u32_be(p + stfs::off::TITLE_ID);

    // Platform / Exec Type / Disc info at 0x364..0x367
    h.platform    = p[stfs::off::PLATFORM];
    h.exec_type   = p[stfs::off::EXEC_TYPE];
    h.disc_number = p[stfs::off::DISC_NUMBER];
    h.disc_in_set = p[stfs::off::DISC_IN_SET];

    // Save Game ID at 0x368 - BIG-ENDIAN
    h.save_game_id = endian::read_u32_be(p + stfs::off::SAVE_GAME_ID);

    // Console ID (5 bytes) at 0x36C
    std::memcpy(h.console_id.data(), p + stfs::off::CONSOLE_ID, 5);

    // Profile ID (8 bytes) at 0x371 - used as xuid by Xenia for path resolution!
    std::memcpy(h.profile_id.data(), p + stfs::off::PROFILE_ID, 8);

    // Volume descriptor at 0x37A - parse separately
    {
        VolumeDescriptor vd;
        XBOX_TRY_ASSIGN(vd, parse_volume_descriptor(
            std::span<const byte>(reinterpret_cast<const byte*>(p + stfs::off::VOLUME_DESCRIPTOR),
                                  stfs::vol::DESCRIPTOR_SIZE)));
        h.volume = vd;
    }

    // Data file count at 0x39D - BIG-ENDIAN
    h.data_file_count = static_cast<i32>(endian::read_u32_be(p + stfs::off::DATA_FILE_COUNT));

    // Data file combined size at 0x3A1 - BIG-ENDIAN
    h.data_file_combined_size = endian::read_u64_be(p + stfs::off::DATA_FILE_COMBINED);

    // *** Volume Type *** (4 bytes BE) at offset 0x3A9
    // Per Xenia's stfs_xbox.h: XContentVolumeType (0=STFS, 1=SVOD)
    {
        u32 vt = endian::read_u32_be(p + stfs::VOLUME_TYPE);
        if (vt == stfs::VOLUME_TYPE_STFS) {
            h.volume_type = VolumeType::Stfs;
        } else if (vt == stfs::VOLUME_TYPE_SVOD) {
            h.volume_type = VolumeType::Svod;
        } else {
            h.volume_type = VolumeType::Unknown;
        }
    }

    // Online creator (8 bytes BE) at 0x3AD
    h.online_creator = endian::read_u64_be(p + stfs::ONLINE_CREATOR);

    // Category (4 bytes BE) at 0x3B5
    h.category = endian::read_u32_be(p + stfs::CATEGORY);

    // Device ID (20 bytes) at 0x3FD
    std::memcpy(h.device_id.data(), p + stfs::off::DEVICE_ID, 20);

    // Display name (12 locales, each 0x80 bytes UTF-16 BE) at 0x411
    {
        const u8* names = p + stfs::off::DISPLAY_NAME;
        for (int i = 0; i < 12; ++i) {
            std::string decoded = str::utf16be_cstr_to_utf8(names + i * 0x80, 0x80);
            if (!decoded.empty()) {
                h.display_name = std::move(decoded);
                break;
            }
        }
    }

    // Display description (12 locales, each 0x80 bytes) at 0xD11
    {
        const u8* descs = p + stfs::off::DISPLAY_DESC;
        for (int i = 0; i < 12; ++i) {
            std::string decoded = str::utf16be_cstr_to_utf8(descs + i * 0x80, 0x80);
            if (!decoded.empty()) {
                h.display_description = std::move(decoded);
                break;
            }
        }
    }

    // Publisher name (0x80 bytes UTF-16 BE) at 0x1611
    h.publisher_name = str::utf16be_cstr_to_utf8(p + stfs::off::PUBLISHER_NAME, 0x80);

    // Title name (0x80 bytes UTF-16 BE) at 0x1691
    h.title_name = str::utf16be_cstr_to_utf8(p + stfs::off::TITLE_NAME, 0x80);

    // Thumbnail sizes
    h.thumbnail_size = static_cast<i32>(endian::read_u32_be(p + stfs::off::THUMBNAIL_SIZE));
    h.title_thumbnail_size = static_cast<i32>(endian::read_u32_be(p + stfs::off::TITLE_THUMB_SIZE));

    return h;
}

} // namespace xbox::stfs
