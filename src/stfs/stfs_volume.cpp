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
#include "xbox/stfs/stfs_volume.hpp"

#include "xbox/core/endian.hpp"
#include "xbox/core/errors.hpp"

#include <cstring>

namespace xbox::stfs {

Result<VolumeDescriptor, Error> parse_volume_descriptor(std::span<const byte> buffer) {
    if (buffer.size() < stfs::vol::DESCRIPTOR_SIZE) {
        return XBOX_STFS_ERROR(InvalidVolumeDescriptor,
            "volume descriptor buffer too small: " + std::to_string(buffer.size()));
    }

    const auto* p = reinterpret_cast<const u8*>(buffer.data());
    VolumeDescriptor vd{};

    vd.reserved = p[stfs::vol::RESERVED];
    vd.block_separation = p[stfs::vol::BLOCK_SEPARATION];

    // File table block count (2 bytes LITTLE-ENDIAN!)
    // Verified against real Minecraft TU file: LE(0x01, 0x00) = 1, not BE = 256
    vd.file_table_block_count = endian::read_u16_le(p + stfs::vol::FILE_TABLE_BLOCK_COUNT);

    // File table block number (3 bytes LITTLE-ENDIAN)
    vd.file_table_block_number = static_cast<u32>(endian::read_u24_le(p + stfs::vol::FILE_TABLE_BLOCK_NUMBER));

    // Top hash table hash (20 bytes SHA1)
    std::memcpy(vd.top_hash_table_hash.data(), p + stfs::vol::TOP_HASH_TABLE_HASH, 0x14);

    // Total allocated blocks (4 bytes BIG-ENDIAN)
    vd.total_allocated_blocks = static_cast<i32>(endian::read_u32_be(p + stfs::vol::TOTAL_ALLOCATED_BLOCKS));

    // Total unallocated blocks (4 bytes BIG-ENDIAN)
    vd.total_unallocated_blocks = static_cast<i32>(endian::read_u32_be(p + stfs::vol::TOTAL_UNALLOCATED_BLOCKS));

    return vd;
}

Result<VolumeDescriptor, Error> parse_volume_descriptor_from_header(
    std::span<const byte> header_buffer) {
    if (header_buffer.size() < stfs::off::VOLUME_DESCRIPTOR + stfs::vol::DESCRIPTOR_SIZE) {
        return XBOX_STFS_ERROR(InvalidHeaderSize,
            "header buffer too small for volume descriptor");
    }
    return parse_volume_descriptor(
        std::span<const byte>(header_buffer.data() + stfs::off::VOLUME_DESCRIPTOR,
                              stfs::vol::DESCRIPTOR_SIZE));
}

} // namespace xbox::stfs
