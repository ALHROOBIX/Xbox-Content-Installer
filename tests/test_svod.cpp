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
#include "xbox/stfs/svod_container.hpp"

#include <cstring>
#include <fstream>
#include <filesystem>

using namespace xbox;
using namespace xbox::stfs;

namespace fs = std::filesystem;

TEST(SvodDescriptor_Parse) {
    std::vector<u8> buf(svod::vol::DESCRIPTOR_SIZE, 0);

    // Set fields
    buf[svod::vol::DESCRIPTOR_LENGTH] = 0x24;
    buf[svod::vol::BLOCK_CACHE_ELEMENT_COUNT] = 8;
    buf[svod::vol::WORKER_THREAD_PROCESSOR] = 2;
    buf[svod::vol::WORKER_THREAD_PRIORITY] = 16;

    // First fragment hash entry (20 bytes)
    for (int i = 0; i < 20; ++i) {
        buf[svod::vol::FIRST_FRAGMENT_HASH_ENTRY + i] = static_cast<u8>(0xAA + i);
    }

    // Features byte - EnhancedGDF bit set
    buf[svod::vol::FEATURES_BYTE] = 0x02;

    // Num data blocks (3 bytes LE) = 1000
    buf[svod::vol::NUM_DATA_BLOCKS_RAW] = 0xE8;
    buf[svod::vol::NUM_DATA_BLOCKS_RAW + 1] = 0x03;
    buf[svod::vol::NUM_DATA_BLOCKS_RAW + 2] = 0x00;

    // Start data block (3 bytes LE) = 2
    buf[svod::vol::START_DATA_BLOCK_RAW] = 2;
    buf[svod::vol::START_DATA_BLOCK_RAW + 1] = 0;
    buf[svod::vol::START_DATA_BLOCK_RAW + 2] = 0;

    auto r = parse_svod_descriptor(
        std::span<const byte>(reinterpret_cast<const byte*>(buf.data()), buf.size()));
    ASSERT_TRUE(r.is_ok());

    const auto& d = r.value();
    EXPECT_EQ(d.descriptor_length, 0x24);
    EXPECT_EQ(d.block_cache_element_count, 8);
    EXPECT_EQ(d.worker_thread_processor, 2);
    EXPECT_EQ(d.worker_thread_priority, 16);
    EXPECT_EQ(d.first_fragment_hash_entry[0], 0xAA);
    EXPECT_EQ(d.first_fragment_hash_entry[19], 0xBD);
    EXPECT_EQ(d.features_byte, 0x02);
    EXPECT_TRUE(d.is_enhanced_gdf());
    EXPECT_EQ(d.num_data_blocks, 1000u);
    EXPECT_EQ(d.start_data_block, 2u);
}

TEST(SvodDescriptor_NonEnhancedGDF) {
    std::vector<u8> buf(svod::vol::DESCRIPTOR_SIZE, 0);
    buf[svod::vol::FEATURES_BYTE] = 0x00;  // No EGDF bit

    auto r = parse_svod_descriptor(
        std::span<const byte>(reinterpret_cast<const byte*>(buf.data()), buf.size()));
    ASSERT_TRUE(r.is_ok());
    EXPECT_FALSE(r.value().is_enhanced_gdf());
}

TEST(SvodBlockToLocation_FirstBlock) {
    // Test block 0 with start_data_block = 0, normal layout
    auto loc = svod_block_to_location(0, 0, SvodLayoutType::MultipleFiles, 0);
    // True block = 0 - 0 = 0
    // file_block = 0 % 82824 = 0
    // file_index = 0 / 82824 = 0
    // level0_table_count = (0 / 408) + 1 = 1
    // offset = 1 * 0x1000 = 0x1000
    // level1_table_count = (1 / 41412) + 1 = 1
    // offset += 1 * 0x1000 = 0x2000
    // block_address = (0 * 0x800) + 0x2000 = 0x2000
    EXPECT_EQ(loc.file_index, 0u);
    EXPECT_EQ(loc.byte_offset, 0x2000ull);
}

TEST(SvodBlockToLocation_EGDFLayout) {
    // EGDF layout adds 2 to the true_block
    auto loc = svod_block_to_location(0, 0, SvodLayoutType::EnhancedGDF, 0);
    // true_block = 0 - 0 + 2 = 2
    // file_block = 2 % 82824 = 2
    // file_index = 0
    // level0_table_count = (2 / 408) + 1 = 1
    // offset = 1 * 0x1000 + 1 * 0x1000 = 0x2000
    // block_address = (2 * 0x800) + 0x2000 = 0x1000 + 0x2000 = 0x3000
    EXPECT_EQ(loc.file_index, 0u);
    EXPECT_EQ(loc.byte_offset, 0x3000ull);
}

TEST(SvodBlockToLocation_SecondFile) {
    // Test block in the second file (BLOCKS_PER_FILE = 0x14388)
    // When file_block exceeds BLOCKS_PER_FILE, file_index increments
    u32 block = 0x14388 + 1;  // Just past first file boundary
    auto loc = svod_block_to_location(block, 0, SvodLayoutType::MultipleFiles, 0);
    EXPECT_EQ(loc.file_index, 1u);  // Should be in second file
}

TEST(SvodDirectoryEntry_Parse) {
    // Build a minimal SVOD directory entry
    // 14 bytes header + name
    std::vector<u8> buf(14 + 8, 0);

    // node_l (LE 16-bit) = 1
    buf[0] = 1; buf[1] = 0;
    // node_r (LE 16-bit) = 2
    buf[2] = 2; buf[3] = 0;
    // data_block (BE 32-bit) = 0x100
    buf[4] = 0; buf[5] = 0; buf[6] = 1; buf[7] = 0;
    // length (BE 32-bit) = 4096
    buf[8] = 0; buf[9] = 0; buf[10] = 0x10; buf[11] = 0;
    // attributes = 0 (file, not directory)
    buf[12] = 0;
    // name_length = 8
    buf[13] = 8;
    // name = "test.bin"
    std::memcpy(buf.data() + 14, "test.bin", 8);

    auto r = parse_svod_directory_entry(buf.data(), buf.size());
    ASSERT_TRUE(r.is_ok());

    const auto& [entry, consumed] = r.value();
    EXPECT_EQ(entry.node_l, 1u);
    EXPECT_EQ(entry.node_r, 2u);
    EXPECT_EQ(entry.data_block, 0x100u);
    EXPECT_EQ(entry.length, 4096u);
    EXPECT_EQ(entry.attributes, 0);
    EXPECT_EQ(entry.name_length, 8);
    EXPECT_EQ(entry.name, "test.bin");
    EXPECT_FALSE(entry.is_directory());
    EXPECT_EQ(consumed, 22u);  // 14 + 8
}

TEST(SvodDirectoryEntry_DirectoryFlag) {
    std::vector<u8> buf(14 + 3, 0);
    buf[12] = 0x10;  // directory attribute bit
    buf[13] = 3;
    std::memcpy(buf.data() + 14, "dir", 3);

    auto r = parse_svod_directory_entry(buf.data(), buf.size());
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value().first.is_directory());
    EXPECT_EQ(r.value().first.name, "dir");
}

TEST(SvodDirectoryEntry_InvalidNameLength) {
    std::vector<u8> buf(14, 0);
    buf[13] = 0;  // name_length = 0 is invalid

    auto r = parse_svod_directory_entry(buf.data(), buf.size());
    EXPECT_FALSE(r.is_ok());

    buf[13] = 255;  // too large
    auto r2 = parse_svod_directory_entry(buf.data(), buf.size());
    EXPECT_FALSE(r2.is_ok());
}

TEST(SvodDirectoryEntry_BufferTooSmall) {
    std::vector<u8> buf(13, 0);  // Less than DIR_ENTRY_SIZE
    auto r = parse_svod_directory_entry(buf.data(), buf.size());
    EXPECT_FALSE(r.is_ok());
}

TEST(Svod_ListDataFiles_Empty) {
    // Test with a non-existent .data directory
    auto tmp = fs::temp_directory_path() / "xbox_svod_test_empty";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    auto header = tmp / "fake.stfs";
    std::ofstream f(header);
    f << "test";
    f.close();

    auto r = list_svod_data_files(header);
    EXPECT_FALSE(r.is_ok());  // Should fail since no .data directory

    fs::remove_all(tmp);
}

TEST(Svod_ListDataFiles_WithFragments) {
    auto tmp = fs::temp_directory_path() / "xbox_svod_test_files";
    fs::remove_all(tmp);

    auto header = tmp / "package.stfs";
    fs::create_directories(tmp);
    std::ofstream f(header);
    f << "test";
    f.close();

    // Create .data directory with fragments
    auto data_dir = tmp / "package.stfs.data";
    fs::create_directories(data_dir);
    {
        std::ofstream f1(data_dir / "Data0000");
        f1 << "data0";
    }
    {
        std::ofstream f2(data_dir / "Data0001");
        f2 << "data1";
    }
    {
        std::ofstream f3(data_dir / "Data0002");
        f3 << "data2";
    }

    auto r = list_svod_data_files(header);
    ASSERT_TRUE(r.is_ok());
    ASSERT_EQ(r.value().size(), 3u);
    // Should be sorted alphabetically (which is also numerically for 4-digit padded)
    EXPECT_EQ(r.value()[0].filename().string(), "Data0000");
    EXPECT_EQ(r.value()[1].filename().string(), "Data0001");
    EXPECT_EQ(r.value()[2].filename().string(), "Data0002");

    fs::remove_all(tmp);
}

TEST(Svod_ListDataFiles_CaseInsensitive) {
    auto tmp = fs::temp_directory_path() / "xbox_svod_test_case";
    fs::remove_all(tmp);

    auto header = tmp / "package.stfs";
    fs::create_directories(tmp);
    std::ofstream f(header);
    f << "test";
    f.close();

    auto data_dir = tmp / "package.stfs.data";
    fs::create_directories(data_dir);
    {
        std::ofstream f1(data_dir / "data0000");  // lowercase
        f1 << "data0";
    }

    auto r = list_svod_data_files(header);
    ASSERT_TRUE(r.is_ok());
    ASSERT_EQ(r.value().size(), 1u);

    fs::remove_all(tmp);
}

TEST(VolumeType_StfsValue) {
    EXPECT_EQ(static_cast<u32>(VolumeType::Stfs), 0u);
    EXPECT_EQ(static_cast<u32>(VolumeType::Svod), 1u);
}

TEST(SvodLayoutType_Values) {
    // Verify the layout type enum values match Xenia's constants
    EXPECT_EQ(static_cast<u32>(SvodLayoutType::Unknown), 0x0u);
    EXPECT_EQ(static_cast<u32>(SvodLayoutType::EnhancedGDF), 0x1u);
    EXPECT_EQ(static_cast<u32>(SvodLayoutType::XSF), 0x2u);
    EXPECT_EQ(static_cast<u32>(SvodLayoutType::SingleFile), 0x4u);
    EXPECT_EQ(static_cast<u32>(SvodLayoutType::MultipleFiles), 0x8u);
}

TEST(SvodConstants_Verify) {
    // Verify SVOD constants match Xenia's svod_container_device.cc
    EXPECT_EQ(svod::BLOCK_SIZE, 0x800ull);
    EXPECT_EQ(svod::HASH_BLOCK_SIZE, 0x1000ull);
    EXPECT_EQ(svod::BLOCKS_PER_L0_HASH, 0x198ull);
    EXPECT_EQ(svod::HASHES_PER_L1_HASH, 0xA1C4ull);
    EXPECT_EQ(svod::BLOCKS_PER_FILE, 0x14388ull);
    EXPECT_EQ(svod::MAX_FILE_SIZE, 0xA290000ull);
    EXPECT_EQ(svod::DIR_ENTRY_SIZE, 0xEull);
}
