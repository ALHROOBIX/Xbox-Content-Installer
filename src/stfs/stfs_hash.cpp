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
#include "xbox/stfs/stfs_hash.hpp"

#include "xbox/core/endian.hpp"
#include "xbox/core/errors.hpp"

#include <cstring>

namespace xbox::stfs {

// ---------------------------------------------------------------------------
// BlockToOffset — exact replica of Xenia's StfsContainerDevice::BlockToOffset
//
// Xenia source (stfs_container_device.cc:178-196):
//   size_t StfsContainerDevice::BlockToOffset(uint64_t block_index) const {
//     uint64_t block = block_index;
//     for (uint32_t i = 0; i < kBlocksHashLevelAmount; i++) {
//       const uint32_t level_base = kBlocksPerHashLevel[i];
//       block += ((block_index + level_base) / level_base) * blocks_per_hash_table_;
//       if (block_index < level_base) { break; }
//     }
//     return xe::round_up(header_size, kBlockSize) + (block << 12);
//   }
// ---------------------------------------------------------------------------
u64 block_to_offset(u32 block_index, u32 header_size,
                    u32 blocks_per_hash_table) noexcept {
    u64 block = block_index;
    for (u32 i = 0; i < BLOCKS_HASH_LEVEL_AMOUNT; ++i) {
        const u32 level_base = BLOCKS_PER_HASH_LEVEL[i];
        block += ((static_cast<u64>(block_index) + level_base) / level_base)
                 * blocks_per_hash_table;
        if (block_index < level_base) {
            break;
        }
    }
    return round_up(header_size, BLOCK_SIZE) + (block << 12);
}

// ---------------------------------------------------------------------------
// BlockToHashBlockNumber — exact replica of Xenia's function
//
// Xenia source (stfs_container_device.cc:198-222):
//   uint32_t BlockToHashBlockNumber(uint32_t block_index, uint32_t hash_level) {
//     if (hash_level == 2) return block_step_[1];
//     if (block_index < kBlocksPerHashLevel[hash_level]) {
//       return hash_level == 0 ? 0 : block_step_[hash_level - 1];
//     }
//     uint32_t block = (block_index / kBlocksPerHashLevel[hash_level]) * block_step_[hash_level];
//     if (hash_level == 0) {
//       block += ((block_index / kBlocksPerHashLevel[1]) + 1) * blocks_per_hash_table_;
//       if (block_index < kBlocksPerHashLevel[1]) { return block; }
//     }
//     return block + blocks_per_hash_table_;
//   }
//
// Where block_step_ is computed in SetupContainer():
//   block_step_[0] = kBlocksPerHashLevel[0] + blocks_per_hash_table_  // 170 + bph
//   block_step_[1] = kBlocksPerHashLevel[1] + ((kBlocksPerHashLevel[0] + 1) * bph)  // 28900 + 171*bph
// ---------------------------------------------------------------------------
u32 block_to_hash_block_number(u32 block_index, u32 hash_level,
                                u32 blocks_per_hash_table) noexcept {
    const u32 block_step_0 = BLOCKS_PER_HASH_LEVEL[0] + blocks_per_hash_table;
    const u32 block_step_1 = BLOCKS_PER_HASH_LEVEL[1] +
                             ((BLOCKS_PER_HASH_LEVEL[0] + 1) * blocks_per_hash_table);

    if (hash_level == 2) {
        return block_step_1;
    }

    if (block_index < BLOCKS_PER_HASH_LEVEL[hash_level]) {
        return hash_level == 0 ? 0 : (hash_level == 1 ? block_step_0 : block_step_1);
    }

    u32 block = (block_index / BLOCKS_PER_HASH_LEVEL[hash_level]) *
                (hash_level == 0 ? block_step_0 : block_step_1);

    if (hash_level == 0) {
        block += ((block_index / BLOCKS_PER_HASH_LEVEL[1]) + 1) * blocks_per_hash_table;
        if (block_index < BLOCKS_PER_HASH_LEVEL[1]) {
            return block;
        }
    }

    return block + blocks_per_hash_table;
}

// ---------------------------------------------------------------------------
// BlockToHashBlockOffset — exact replica of Xenia's function
// ---------------------------------------------------------------------------
u64 level1_hash_table_offset(u32 block_index, u32 header_size,
                              u32 blocks_per_hash_table) noexcept {
    const u64 block = block_to_hash_block_number(block_index, 0, blocks_per_hash_table);
    return round_up(header_size, BLOCK_SIZE) + (block << 12);
}

u64 level1_hash_table_offset_secondary(u32 block_index, u32 header_size,
                                        u32 blocks_per_hash_table) noexcept {
    // For writable packages (bph=2), the secondary hash table is 1 block
    // after the primary. For readonly (bph=1), there's only one table.
    u64 primary = level1_hash_table_offset(block_index, header_size, blocks_per_hash_table);
    if (blocks_per_hash_table == 1) {
        return primary;  // no secondary for readonly
    }
    return primary + BLOCK_SIZE;
}

// ---------------------------------------------------------------------------
// Parse a single hash entry (24 bytes)
// Per Xenia: info_raw is be<uint32_t> (BIG-ENDIAN)
// ---------------------------------------------------------------------------
HashEntry parse_hash_entry(const void* ptr) noexcept {
    HashEntry e{};
    const auto* p = static_cast<const u8*>(ptr);
    std::memcpy(e.hash.data(), p, 20);
    // info_raw is BIG-ENDIAN (per Xenia's be<uint32_t>)
    e.info_raw = endian::read_u32_be(p + 20);
    return e;
}

std::array<HashEntry, 0xAA> parse_hash_table(const void* ptr) noexcept {
    std::array<HashEntry, 0xAA> table{};
    const auto* p = static_cast<const u8*>(ptr);
    for (std::size_t i = 0; i < 0xAA; ++i) {
        table[i] = parse_hash_entry(p + i * HASH_ENTRY_SIZE);
    }
    return table;
}

bool verify_block_hash(std::span<const byte> block_data,
                       const SHA1::Digest& stored_hash) noexcept {
    auto computed = crypto::SHA1::compute(block_data.data(),
                                          std::min<std::size_t>(block_data.size(), BLOCK_SIZE));
    return crypto::SHA1::equal(computed, stored_hash);
}

const HashEntry& lookup_hash_entry(
    const std::array<HashEntry, 0xAA>& table,
    u32 block_in_group) noexcept {
    return table[block_in_group % HASH_TABLE_BLOCKS_STEP];
}

std::string HashEntry::format() const {
    std::string out;
    out.reserve(80);
    out.append("hash=").append(crypto::SHA1::to_hex(hash));
    char buf[32];
    std::snprintf(buf, sizeof(buf), " info_raw=0x%08X", info_raw);
    out.append(buf);
    std::snprintf(buf, sizeof(buf), " next_block=%u", next_block());
    out.append(buf);
    out.append(" state=").append(std::to_string(static_cast<int>(allocation_state())));
    return out;
}

} // namespace xbox::stfs
