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
#include "xbox/crypto/sha1.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>

namespace xbox::stfs {

// Re-export SHA1 from the crypto namespace for convenience
using xbox::crypto::SHA1;

// Hash allocation state (per Xenia's StfsHashState enum)
// Encoded in bits 30-31 of the 4-byte info_raw field (BE uint32)
enum class HashState : u8 {
    kFree  = 0,  // bits 30-31 = 00
    kFree2 = 1,  // bits 30-31 = 01
    kInUse = 2,  // bits 30-31 = 10
};

// Hash status byte values (byte at offset 0x14 of each 0x18-byte entry)
// These correspond to HashState via: state = (status_byte >> 6) & 3
namespace hash_status {
    constexpr u8 UNUSED        = 0x00;  // state 0 (kFree)
    constexpr u8 FREE          = 0x40;  // state 1 (kFree2)
    constexpr u8 USED          = 0x80;  // state 2 (kInUse)
    constexpr u8 NEWLY_ALLOC   = 0xC0;  // state 3 (undefined in Xenia)
    constexpr u8 MASK          = 0xC0;
} // namespace hash_status

// Single 24-byte hash entry (per Xenia's StfsHashEntry)
// Layout:
//   offset 0x00: 0x14 bytes  - SHA1 of the corresponding data block
//   offset 0x14: 4 bytes BE  - info_raw (bits 0-23 = next_block, bits 30-31 = state)
struct HashEntry {
    SHA1::Digest hash{};
    u32 info_raw{0};  // BE uint32 (read as-is, use accessor methods)

    // Next block in chain (bits 0-23 of info_raw)
    [[nodiscard]] u32 next_block() const noexcept {
        return info_raw & 0xFFFFFF;
    }

    // Allocation state (bits 30-31 of info_raw)
    [[nodiscard]] HashState allocation_state() const noexcept {
        return static_cast<HashState>((info_raw >> 30) & 0x3);
    }

    // Convenience: status byte (the MSB of info_raw)
    [[nodiscard]] u8 status_byte() const noexcept {
        return static_cast<u8>((info_raw >> 24) & 0xFF);
    }

    // Convenience methods matching old API
    [[nodiscard]] bool is_unused()      const noexcept { return allocation_state() == HashState::kFree;  }
    [[nodiscard]] bool is_free()        const noexcept { return allocation_state() == HashState::kFree2; }
    [[nodiscard]] bool is_used()        const noexcept { return allocation_state() == HashState::kInUse; }
    [[nodiscard]] bool is_newly_alloc() const noexcept { return allocation_state() == static_cast<HashState>(3); }

    [[nodiscard]] std::string format() const;
};

// ---------------------------------------------------------------------------
// Block-to-offset conversion (Xenia's BlockToOffset, exact replica)
//
// Parameters:
//   block_index: the data block number
//   header_size: the header_size field from the STFS header (offset 0x340, BE)
//   blocks_per_hash_table: 1 (readonly/LIVE/PIRS) or 2 (writable/CON)
//
// Returns the absolute byte offset of the data block in the file.
// ---------------------------------------------------------------------------
[[nodiscard]] u64 block_to_offset(u32 block_index, u32 header_size,
                                   u32 blocks_per_hash_table) noexcept;

// ---------------------------------------------------------------------------
// Hash-table block number (Xenia's BlockToHashBlockNumber, exact replica)
//
// Returns the logical block number of the hash table that contains the
// entry for `block_index` at the given `hash_level` (0, 1, or 2).
// ---------------------------------------------------------------------------
[[nodiscard]] u32 block_to_hash_block_number(u32 block_index, u32 hash_level,
                                              u32 blocks_per_hash_table) noexcept;

// ---------------------------------------------------------------------------
// Hash-table byte offset (Xenia's BlockToHashBlockOffset)
//
// Returns the absolute byte offset of the hash table for `block_index`.
// ---------------------------------------------------------------------------
[[nodiscard]] u64 level1_hash_table_offset(u32 block_index, u32 header_size,
                                            u32 blocks_per_hash_table) noexcept;

// Secondary hash table offset (for writable packages with 2 backing blocks)
// Returns the offset of the secondary hash table (1 block after primary)
[[nodiscard]] u64 level1_hash_table_offset_secondary(u32 block_index, u32 header_size,
                                                       u32 blocks_per_hash_table) noexcept;

// ---------------------------------------------------------------------------
// Parse a single hash entry from a 24-byte buffer.
// Reads info_raw as BIG-ENDIAN (per Xenia's be<uint32_t>).
// ---------------------------------------------------------------------------
[[nodiscard]] HashEntry parse_hash_entry(const void* ptr) noexcept;

// ---------------------------------------------------------------------------
// Parse a full hash table (170 entries) from a 0x1788-byte buffer.
// ---------------------------------------------------------------------------
[[nodiscard]] std::array<HashEntry, 0xAA> parse_hash_table(const void* ptr) noexcept;

// ---------------------------------------------------------------------------
// Compute SHA1 of a single data block (0x1000 bytes) and compare to stored hash.
// ---------------------------------------------------------------------------
[[nodiscard]] bool verify_block_hash(std::span<const byte> block_data,
                                     const SHA1::Digest& stored_hash) noexcept;

// ---------------------------------------------------------------------------
// Convenience: look up the hash entry for a specific block index in a
// pre-loaded level-1 hash table.
// ---------------------------------------------------------------------------
[[nodiscard]] const HashEntry& lookup_hash_entry(
    const std::array<HashEntry, 0xAA>& table,
    u32 block_in_group) noexcept;

// Helper: round up to nearest multiple (Xenia's round_up)
[[nodiscard]] constexpr u64 round_up(u64 value, u64 multiple) noexcept {
    return ((value + multiple - 1) / multiple) * multiple;
}

// Helper: determine blocks_per_hash_table from package type
// CON = writable (2), LIVE/PIRS = readonly (1)
[[nodiscard]] constexpr u32 get_blocks_per_hash_table(bool is_readonly) noexcept {
    return is_readonly ? 1 : 2;
}

} // namespace xbox::stfs
