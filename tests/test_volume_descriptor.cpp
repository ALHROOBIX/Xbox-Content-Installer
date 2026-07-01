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
#include "test_framework.hpp"
#include "xbox/core/errors.hpp"
#include "xbox/stfs/stfs_hash.hpp"
#include "xbox/stfs/stfs_volume.hpp"

using namespace xbox;
using namespace xbox::stfs;

// Standard header sizes
constexpr u32 HEADER_SIZE_STANDARD = 0x971A;  // Standard STFS header
constexpr u32 HEADER_SIZE_MINECRAFT = 0xAD0E; // Minecraft TU header

TEST(BlockToOffset_Group0_ReadOnly) {
    // LIVE/PIRS (readonly, bph=1), header_size = 0xAD0E
    // round_up(0xAD0E, 0x1000) = 0xB000
    // Block 0: block = 0 + ((0+170)/170)*1 = 0 + 1 = 1
    // offset = 0xB000 + (1 << 12) = 0xB000 + 0x1000 = 0xC000
    EXPECT_EQ(block_to_offset(0, HEADER_SIZE_MINECRAFT, 1), 0xC000ull);
}

TEST(BlockToOffset_Group0_Standard) {
    // Standard header (0x971A), bph=1
    // round_up(0x971A, 0x1000) = 0xA000
    // Block 0: offset = 0xA000 + (1 << 12) = 0xB000
    EXPECT_EQ(block_to_offset(0, HEADER_SIZE_STANDARD, 1), 0xB000ull);
}

TEST(BlockToOffset_Group1_ReadOnly) {
    // Block 170 (start of group 1), bph=1, header=0xAD0E
    // Verified against real Minecraft TU file: 0xB8000
    EXPECT_EQ(block_to_offset(170, HEADER_SIZE_MINECRAFT, 1), 0xB8000ull);
}

TEST(BlockToOffset_Group2_ReadOnly) {
    // Block 340 (start of group 2), bph=1, header=0xAD0E
    // Verified against real Minecraft TU file: 0x163000
    EXPECT_EQ(block_to_offset(340, HEADER_SIZE_MINECRAFT, 1), 0x163000ull);
}

TEST(BlockToOffset_Group0_Writable) {
    // CON (writable, bph=2), header_size = 0x971A
    // round_up(0x971A, 0x1000) = 0xA000
    // Block 0: block = 0 + ((0+170)/170)*2 = 0 + 2 = 2
    // offset = 0xA000 + (2 << 12) = 0xA000 + 0x2000 = 0xC000
    EXPECT_EQ(block_to_offset(0, HEADER_SIZE_STANDARD, 2), 0xC000ull);
}

TEST(BlockToOffset_Group1_Writable) {
    // Block 170, bph=2, header=0x971A
    // block = 170 + ((170+170)/170)*2 + ((170+28900)/28900)*2
    //       = 170 + 4 + 2 = 176
    // offset = 0xA000 + (176 << 12) = 0xA000 + 0xB0000 = 0xBA000
    EXPECT_EQ(block_to_offset(170, HEADER_SIZE_STANDARD, 2), 0xBA000ull);
}

TEST(HashTable_Offset_Group0_ReadOnly) {
    // Hash table for block 0, bph=1, header=0xAD0E
    // BlockToHashBlockNumber(0, 0) = 0 (since 0 < 170, return 0)
    // offset = 0xB000 + (0 << 12) = 0xB000
    EXPECT_EQ(level1_hash_table_offset(0, HEADER_SIZE_MINECRAFT, 1), 0xB000ull);
}

TEST(HashTable_Offset_Group1_ReadOnly) {
    // Hash table for block 170, bph=1, header=0xAD0E
    // Verified against real Minecraft TU file: 0xB7000
    EXPECT_EQ(level1_hash_table_offset(170, HEADER_SIZE_MINECRAFT, 1), 0xB7000ull);
}

TEST(HashTable_Offset_Group2_ReadOnly) {
    // Hash table for block 340, bph=1, header=0xAD0E
    // Verified against real Minecraft TU file: 0x162000
    EXPECT_EQ(level1_hash_table_offset(340, HEADER_SIZE_MINECRAFT, 1), 0x162000ull);
}

TEST(HashTable_Offset_Group0_Writable) {
    // CON (bph=2), header=0x971A
    // BlockToHashBlockNumber(0, 0) = 0
    // Primary offset = 0xA000 + (0 << 12) = 0xA000
    EXPECT_EQ(level1_hash_table_offset(0, HEADER_SIZE_STANDARD, 2), 0xA000ull);
    // Secondary = primary + 0x1000 = 0xB000
    EXPECT_EQ(level1_hash_table_offset_secondary(0, HEADER_SIZE_STANDARD, 2), 0xB000ull);
}

TEST(HashEntry_Parse) {
    u8 buf[24] = {0};
    // SHA1: 20 bytes of 0xAA
    for (int i = 0; i < 20; ++i) buf[i] = 0xAA;
    // info_raw (BE uint32): 0x80FFFFFF
    //   next_block = 0xFFFFFF (end of chain)
    //   allocation_state = (0x80 >> 6) & 3 = 2 (kInUse)
    buf[20] = 0x80;
    buf[21] = 0xFF;
    buf[22] = 0xFF;
    buf[23] = 0xFF;

    auto e = parse_hash_entry(buf);
    EXPECT_EQ(e.hash[0], 0xAA);
    EXPECT_EQ(e.hash[19], 0xAA);
    EXPECT_EQ(e.info_raw, 0x80FFFFFFu);
    EXPECT_EQ(e.next_block(), 0xFFFFFFu);
    EXPECT_TRUE(e.is_used());
    EXPECT_EQ(e.status_byte(), 0x80);
}

TEST(HashEntry_StatusFlags) {
    HashEntry e{};

    // state 0 (kFree) - status byte 0x00
    e.info_raw = 0x00000000;
    EXPECT_TRUE(e.is_unused());
    EXPECT_EQ(e.allocation_state(), HashState::kFree);

    // state 1 (kFree2) - status byte 0x40
    e.info_raw = 0x40000000;
    EXPECT_TRUE(e.is_free());
    EXPECT_EQ(e.allocation_state(), HashState::kFree2);

    // state 2 (kInUse) - status byte 0x80
    e.info_raw = 0x80000000;
    EXPECT_TRUE(e.is_used());
    EXPECT_EQ(e.allocation_state(), HashState::kInUse);
}

TEST(HashEntry_NextBlock) {
    HashEntry e{};
    // info_raw = 0x80000123 → next_block = 0x123, state = kInUse
    e.info_raw = 0x80000123;
    EXPECT_EQ(e.next_block(), 0x123u);
    EXPECT_TRUE(e.is_used());
}

TEST(HashEntry_ParseFullTable) {
    // 170 entries * 24 bytes
    std::vector<u8> buf(HASH_TABLE_BLOCKS_STEP * HASH_ENTRY_SIZE, 0);

    // Set entry 5 to a known state
    auto* e5 = buf.data() + 5 * HASH_ENTRY_SIZE;
    for (int i = 0; i < 20; ++i) e5[i] = 0xBB;
    // info_raw = 0xC0020003 (state=3, next_block=0x20003)
    e5[20] = 0xC0;
    e5[21] = 0x02;
    e5[22] = 0x00;
    e5[23] = 0x03;

    auto table = parse_hash_table(buf.data());
    EXPECT_EQ(table[5].hash[0], 0xBB);
    EXPECT_EQ(table[5].info_raw, 0xC0020003u);
    EXPECT_EQ(table[5].next_block(), 0x20003u);
}

TEST(VolumeDescriptor_BasicParse) {
    std::vector<u8> buf(stfs::vol::DESCRIPTOR_SIZE, 0);

    // File table block count (2 bytes LE) = 5
    buf[stfs::vol::FILE_TABLE_BLOCK_COUNT] = 5;
    buf[stfs::vol::FILE_TABLE_BLOCK_COUNT + 1] = 0;

    // File table block number (3 bytes LE) = 100
    buf[stfs::vol::FILE_TABLE_BLOCK_NUMBER] = 100;
    buf[stfs::vol::FILE_TABLE_BLOCK_NUMBER + 1] = 0;
    buf[stfs::vol::FILE_TABLE_BLOCK_NUMBER + 2] = 0;

    // Total allocated blocks (4 bytes BE) = 1000
    u32 v = 1000u;
    buf[stfs::vol::TOTAL_ALLOCATED_BLOCKS + 0] = static_cast<u8>(v >> 24);
    buf[stfs::vol::TOTAL_ALLOCATED_BLOCKS + 1] = static_cast<u8>(v >> 16);
    buf[stfs::vol::TOTAL_ALLOCATED_BLOCKS + 2] = static_cast<u8>(v >> 8);
    buf[stfs::vol::TOTAL_ALLOCATED_BLOCKS + 3] = static_cast<u8>(v);

    auto r = parse_volume_descriptor(
        std::span<const byte>(reinterpret_cast<const byte*>(buf.data()), buf.size()));
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value().file_table_block_count, 5u);
    EXPECT_EQ(r.value().file_table_block_number, 100u);
    EXPECT_EQ(r.value().total_allocated_blocks, 1000);
}

TEST(RoundUp_Function) {
    EXPECT_EQ(round_up(0x971A, 0x1000), 0xA000ull);
    EXPECT_EQ(round_up(0xAD0E, 0x1000), 0xB000ull);
    EXPECT_EQ(round_up(0xA000, 0x1000), 0xA000ull);  // already aligned
    EXPECT_EQ(round_up(0, 0x1000), 0ull);
    EXPECT_EQ(round_up(1, 0x1000), 0x1000ull);
}

TEST(GetBlocksPerHashTable) {
    EXPECT_EQ(get_blocks_per_hash_table(true), 1u);   // readonly
    EXPECT_EQ(get_blocks_per_hash_table(false), 2u);  // writable
}
