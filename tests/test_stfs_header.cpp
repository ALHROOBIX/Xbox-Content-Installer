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
#include "xbox/stfs/stfs_header.hpp"
#include "xbox/stfs/stfs_metadata.hpp"
#include "xbox/stfs/stfs_volume.hpp"

#include <cstring>
#include <vector>

using namespace xbox;
using namespace xbox::stfs;

namespace {

// Build a minimal valid STFS LIVE header for testing.
// All fields are deterministic; we use real Title ID 0x415607ED (Halo 3).
std::vector<u8> make_test_header() {
    std::vector<u8> buf(stfs::HEADER_SIZE, 0);

    // Magic: "LIVE"
    buf[0] = 'L'; buf[1] = 'I'; buf[2] = 'V'; buf[3] = 'E';

    // Package signature at offset 0x04 (0x100 bytes - leave zeros)

    // Content ID at 0x32C (20 bytes) - use a fixed pattern
    for (int i = 0; i < 20; ++i) {
        buf[stfs::off::CONTENT_ID + i] = static_cast<u8>(0x10 + i);
    }

    // Entry ID at 0x340 (4 bytes LITTLE-ENDIAN)
    // Use a value that does NOT trigger two-hash-table layout.
    // ((entry_id + 0xFFF) & 0xF000) >> 0xC must NOT equal 0xB.
    // entry_id = 0x00000001 -> (0x1000 & 0xF000) >> 0xC = 1 != 0xB. Good.
    u32 entry_id = 0x00000001;
    std::memcpy(buf.data() + stfs::off::ENTRY_ID, &entry_id, 4);

    // Content type at 0x344 (4 bytes BIG-ENDIAN) - Title Update
    u32 ct_be = 0x000B0000u;
    ct_be = ((ct_be & 0xFF) << 24) | ((ct_be & 0xFF00) << 8) |
            ((ct_be & 0xFF0000) >> 8) | ((ct_be & 0xFF000000) >> 24);
    std::memcpy(buf.data() + stfs::off::CONTENT_TYPE, &ct_be, 4);

    // Metadata version at 0x348 (4 bytes BIG-ENDIAN) = 1
    u32 mv_be = 0x00000001;
    mv_be = ((mv_be & 0xFF) << 24) | ((mv_be & 0xFF00) << 8) |
            ((mv_be & 0xFF0000) >> 8) | ((mv_be & 0xFF000000) >> 24);
    std::memcpy(buf.data() + stfs::off::METADATA_VERSION, &mv_be, 4);

    // Title ID at 0x360 (4 bytes BIG-ENDIAN) = 0x415607ED
    u32 tid_be = 0x415607EDu;
    tid_be = ((tid_be & 0xFF) << 24) | ((tid_be & 0xFF00) << 8) |
             ((tid_be & 0xFF0000) >> 8) | ((tid_be & 0xFF000000) >> 24);
    std::memcpy(buf.data() + stfs::off::TITLE_ID, &tid_be, 4);

    // Volume descriptor at 0x37A (0x24 bytes)
    // Use simple values: file_table_block_number=2, file_table_block_count=1
    u8* vd = buf.data() + stfs::off::VOLUME_DESCRIPTOR;
    vd[stfs::vol::RESERVED] = 0;
    vd[stfs::vol::BLOCK_SEPARATION] = 0;  // single hash table per group
    // File table block count (2 bytes LITTLE-ENDIAN) = 1
    vd[stfs::vol::FILE_TABLE_BLOCK_COUNT] = 1;       // LE low byte
    vd[stfs::vol::FILE_TABLE_BLOCK_COUNT + 1] = 0;   // LE high byte
    // File table block number (3 bytes LITTLE-ENDIAN) = 2
    vd[stfs::vol::FILE_TABLE_BLOCK_NUMBER] = 2;
    vd[stfs::vol::FILE_TABLE_BLOCK_NUMBER + 1] = 0;
    vd[stfs::vol::FILE_TABLE_BLOCK_NUMBER + 2] = 0;
    // Top hash table hash (20 bytes - leave zeros)
    // Total allocated blocks (4 bytes BE) = 100
    u32 tab_be = 100u;
    tab_be = ((tab_be & 0xFF) << 24) | ((tab_be & 0xFF00) << 8) |
             ((tab_be & 0xFF0000) >> 8) | ((tab_be & 0xFF000000) >> 24);
    std::memcpy(vd + stfs::vol::TOTAL_ALLOCATED_BLOCKS, &tab_be, 4);

    // Display name (UTF-16 BE) at 0x411 - set first locale to "Test Package"
    {
        const char* name = "Test Package";
        std::size_t base = stfs::off::DISPLAY_NAME;
        for (std::size_t i = 0; name[i]; ++i) {
            buf[base + i*2] = 0;  // high byte
            buf[base + i*2 + 1] = static_cast<u8>(name[i]);  // low byte
        }
    }

    return buf;
}

} // namespace

TEST(StfsHeader_IdentifyPackageType) {
    EXPECT_EQ(identify_package_type("CON "), PackageType::CON);
    EXPECT_EQ(identify_package_type("PIRS"), PackageType::PIRS);
    EXPECT_EQ(identify_package_type("LIVE"), PackageType::LIVE);
    EXPECT_EQ(identify_package_type("XXXX"), PackageType::Unknown);
}

TEST(StfsHeader_ParseMinimal) {
    auto buf = make_test_header();
    auto r = parse_header(std::span<const byte>(reinterpret_cast<const byte*>(buf.data()), buf.size()));
    ASSERT_TRUE(r.is_ok());

    const auto& h = r.value();
    EXPECT_EQ(static_cast<int>(h.type), static_cast<int>(PackageType::LIVE));
    EXPECT_EQ(h.title_id, 0x415607EDu);
    EXPECT_EQ(h.content_type, 0x000B0000u);
    EXPECT_EQ(h.metadata_version, 1u);
    EXPECT_FALSE(h.uses_two_hash_tables());  // LIVE is readonly, so 1 hash table
    EXPECT_EQ(h.blocks_per_hash_table(), 1u);  // LIVE = readonly = 1
    EXPECT_EQ(h.display_name, "Test Package");
    EXPECT_TRUE(h.content_type_name().find("Title Update") != std::string::npos);
}

TEST(StfsHeader_InvalidMagic) {
    std::vector<u8> buf(stfs::HEADER_SIZE, 0);
    buf[0] = 'X'; buf[1] = 'X'; buf[2] = 'X'; buf[3] = 'X';
    auto r = parse_header(std::span<const byte>(reinterpret_cast<const byte*>(buf.data()), buf.size()));
    EXPECT_FALSE(r.is_ok());
}

TEST(StfsHeader_TooSmall) {
    std::vector<u8> buf(100, 0);
    auto r = parse_header(std::span<const byte>(reinterpret_cast<const byte*>(buf.data()), buf.size()));
    EXPECT_FALSE(r.is_ok());
}

TEST(StfsHeader_CON_TwoHashTables) {
    // CON packages use 2 hash tables per group (writable)
    auto buf = make_test_header();
    // Change magic to CON
    buf[0] = 'C'; buf[1] = 'O'; buf[2] = 'N'; buf[3] = ' ';

    auto r = parse_header(std::span<const byte>(reinterpret_cast<const byte*>(buf.data()), buf.size()));
    ASSERT_TRUE(r.is_ok());
    EXPECT_TRUE(r.value().uses_two_hash_tables());  // CON is writable
    EXPECT_EQ(r.value().blocks_per_hash_table(), 2u);  // CON = 2
}

TEST(StfsVolumeDescriptor_Parse) {
    auto buf = make_test_header();
    auto vd_r = parse_volume_descriptor_from_header(
        std::span<const byte>(reinterpret_cast<const byte*>(buf.data()), buf.size()));
    ASSERT_TRUE(vd_r.is_ok());
    const auto& vd = vd_r.value();
    EXPECT_EQ(vd.file_table_block_count, 1);
    EXPECT_EQ(vd.file_table_block_number, 2u);
    EXPECT_EQ(vd.total_allocated_blocks, 100);
    EXPECT_FALSE(vd.has_two_hash_tables());
}

TEST(StfsMetadata_QuickReadTitleId) {
    auto buf = make_test_header();
    auto r = quick_read_title_id(std::span<const byte>(reinterpret_cast<const byte*>(buf.data()), buf.size()));
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 0x415607EDu);
}

TEST(StfsMetadata_QuickReadContentType) {
    auto buf = make_test_header();
    auto r = quick_read_content_type(std::span<const byte>(reinterpret_cast<const byte*>(buf.data()), buf.size()));
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 0x000B0000u);
}

TEST(StfsMetadata_ClassifyContentType) {
    EXPECT_TRUE(classify_content_type(0x000B0000u).find("Title Update") != std::string::npos);
    EXPECT_EQ(classify_content_type(0x00000002u), "DLC / Marketplace Content");
    EXPECT_EQ(classify_content_type(0x00000001u), "Saved Game");
    EXPECT_EQ(classify_content_type(0x00030000u), "Theme");
    EXPECT_EQ(classify_content_type(0x000D0000u), "Arcade Title");
    // Unknown
    EXPECT_TRUE(classify_content_type(0xFFFFFFFFu).find("Unknown") != std::string::npos);
}

TEST(StfsMetadata_IsTitleUpdate) {
    EXPECT_TRUE(is_title_update(0x000B0000u));
    EXPECT_FALSE(is_title_update(0x00000002u));
    EXPECT_TRUE(is_dlc(0x00000002u));
    EXPECT_FALSE(is_dlc(0x000B0000u));
}
