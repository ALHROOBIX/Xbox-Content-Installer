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
#include "xbox/core/endian.hpp"
#include "xbox/core/types.hpp"
#include "xbox/crypto/sha1.hpp"
#include "xbox/stfs/stfs_header.hpp"
#include "xbox/stfs/stfs_hash.hpp"
#include "xbox/utils/string_utils.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace xbox;
using namespace xbox::stfs;

namespace {
void write_u32_be(std::vector<u8>& buf, std::size_t offset, u32 v) {
    buf[offset + 0] = static_cast<u8>((v >> 24) & 0xFF);
    buf[offset + 1] = static_cast<u8>((v >> 16) & 0xFF);
    buf[offset + 2] = static_cast<u8>((v >>  8) & 0xFF);
    buf[offset + 3] = static_cast<u8>( v        & 0xFF);
}

void write_u32_le(std::vector<u8>& buf, std::size_t offset, u32 v) {
    buf[offset + 0] = static_cast<u8>( v        & 0xFF);
    buf[offset + 1] = static_cast<u8>((v >>  8) & 0xFF);
    buf[offset + 2] = static_cast<u8>((v >> 16) & 0xFF);
    buf[offset + 3] = static_cast<u8>((v >> 24) & 0xFF);
}

void write_u64_be(std::vector<u8>& buf, std::size_t offset, u64 v) {
    for (int i = 0; i < 8; ++i) {
        buf[offset + i] = static_cast<u8>((v >> (56 - 8*i)) & 0xFF);
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "Usage: " << argv[0] << " <output_file> <title_id_hex> <content_type_hex> <content_filename> <content_data>\n";
        return 1;
    }

    std::string out_path = argv[1];
    u32 title_id = static_cast<u32>(std::stoul(argv[2], nullptr, 16));
    u32 content_type = static_cast<u32>(std::stoul(argv[3], nullptr, 16));
    std::string filename = argv[4];
    std::string filedata = argv[5];

    // Layout (Xenia-accurate, LIVE = readonly, bph=1):
    //   header_size = 0x971A → round_up(0x971A, 0x1000) = 0xA000
    //   Block 0 (file table): offset = 0xA000 + (1 << 12) = 0xB000
    //   Block 1 (file data):  offset = 0xA000 + (2 << 12) = 0xC000
    //   Hash table for block 0: offset = 0xA000 + (0 << 12) = 0xA000
    //
    // Total file size: 0xD000 (header + hash table + 2 data blocks)

    constexpr u32 HEADER_SIZE = 0x971A;
    constexpr u32 BPH = 1;  // LIVE = readonly
    constexpr u64 BASE_OFFSET = 0xA000;  // round_up(0x971A, 0x1000)

    std::vector<u8> buf(0xD000, 0);

    // Magic: LIVE
    buf[0] = 'L'; buf[1] = 'I'; buf[2] = 'V'; buf[3] = 'E';

    // License entries (16 × 16 bytes) at offset 0x22C
    write_u64_be(buf, stfs::off::LICENSE_ENTRIES, 0);           // licensee_id = 0
    write_u32_be(buf, stfs::off::LICENSE_ENTRIES + 8, 0xFFFFFFFFu);  // license_bits
    write_u32_be(buf, stfs::off::LICENSE_ENTRIES + 12, 1u);     // license_flags = 1 (active)

    // Content ID at 0x32C (20 bytes) - hash of inputs for uniqueness
    {
        std::string seed = std::to_string(title_id) + ":" +
                           std::to_string(content_type) + ":" +
                           filename + ":" + filedata;
        auto digest = crypto::SHA1::compute(seed);
        std::memcpy(buf.data() + stfs::off::CONTENT_ID, digest.data(), 20);
    }

    // Header size at 0x340 (BE) = 0x971A
    write_u32_be(buf, stfs::HEADER_SIZE_FIELD, HEADER_SIZE);

    // Content type at 0x344 (BE)
    write_u32_be(buf, stfs::off::CONTENT_TYPE, content_type);

    // Metadata version at 0x348 (BE) = 1
    write_u32_be(buf, stfs::off::METADATA_VERSION, 1);

    // Content size at 0x34C (BE 8 bytes)
    write_u32_be(buf, stfs::off::CONTENT_SIZE, 0);
    write_u32_be(buf, stfs::off::CONTENT_SIZE + 4, static_cast<u32>(filedata.size()));

    // Title ID at 0x360 (BE)
    write_u32_be(buf, stfs::off::TITLE_ID, title_id);

    // Profile ID at 0x371 (8 bytes BE)
    write_u64_be(buf, stfs::off::PROFILE_ID, 0x0000000000000000ull);

    // Volume type at 0x3A9 (BE) = 0 (STFS)
    write_u32_be(buf, stfs::VOLUME_TYPE, stfs::VOLUME_TYPE_STFS);

    // Volume descriptor at 0x37A (0x24 bytes)
    u8* vd = buf.data() + stfs::off::VOLUME_DESCRIPTOR;
    vd[stfs::vol::RESERVED] = 0;
    vd[stfs::vol::BLOCK_SEPARATION] = 1;  // non-zero = readonly (LIVE)
    // File table block count (2 bytes LE) = 1
    vd[stfs::vol::FILE_TABLE_BLOCK_COUNT] = 1;
    vd[stfs::vol::FILE_TABLE_BLOCK_COUNT + 1] = 0;
    // File table block number (3 bytes LE) = 0
    vd[stfs::vol::FILE_TABLE_BLOCK_NUMBER] = 0;
    vd[stfs::vol::FILE_TABLE_BLOCK_NUMBER + 1] = 0;
    vd[stfs::vol::FILE_TABLE_BLOCK_NUMBER + 2] = 0;
    // Total allocated blocks (4 bytes BE) = 2
    u32 tab = 2u;
    vd[stfs::vol::TOTAL_ALLOCATED_BLOCKS + 0] = static_cast<u8>(tab >> 24);
    vd[stfs::vol::TOTAL_ALLOCATED_BLOCKS + 1] = static_cast<u8>(tab >> 16);
    vd[stfs::vol::TOTAL_ALLOCATED_BLOCKS + 2] = static_cast<u8>(tab >>  8);
    vd[stfs::vol::TOTAL_ALLOCATED_BLOCKS + 3] = static_cast<u8>(tab);
    vd[stfs::vol::TOTAL_UNALLOCATED_BLOCKS + 0] = 0;
    vd[stfs::vol::TOTAL_UNALLOCATED_BLOCKS + 1] = 0;
    vd[stfs::vol::TOTAL_UNALLOCATED_BLOCKS + 2] = 0;
    vd[stfs::vol::TOTAL_UNALLOCATED_BLOCKS + 3] = 0;

    // Display name (UTF-16 BE) at 0x411
    {
        const char* name = "Test DLC Package";
        std::size_t base = stfs::off::DISPLAY_NAME;
        for (std::size_t i = 0; name[i]; ++i) {
            buf[base + i*2] = 0;
            buf[base + i*2 + 1] = static_cast<u8>(name[i]);
        }
    }

    // Build data block 0 (file table) at offset 0xB000
    // Per formula: block_to_offset(0, 0x971A, 1) = 0xA000 + (1 << 12) = 0xB000
    u8* ft = buf.data() + 0xB000;
    std::strncpy(reinterpret_cast<char*>(ft), filename.c_str(), 40);
    ft[0x28] = static_cast<u8>(filename.size() & 0x3F);  // name length, no directory flag
    ft[0x29] = 1; ft[0x2A] = 0; ft[0x2B] = 0;  // blocks allocated (LE 24-bit) = 1
    ft[0x2C] = 1; ft[0x2D] = 0; ft[0x2E] = 0;  // copy
    ft[0x2F] = 1; ft[0x30] = 0; ft[0x31] = 0;  // starting block (LE 24-bit) = 1
    ft[0x32] = 0xFF; ft[0x33] = 0xFF;          // path indicator (BE) = root
    write_u32_be(buf, 0xB000 + 0x34, static_cast<u32>(filedata.size()));

    // Build data block 1 (file content) at offset 0xC000
    // Per formula: block_to_offset(1, 0x971A, 1) = 0xA000 + (2 << 12) = 0xC000
    std::memcpy(buf.data() + 0xC000, filedata.data(), filedata.size());

    // Compute SHA1 of both data blocks
    auto block0_hash = crypto::SHA1::compute(buf.data() + 0xB000, stfs::BLOCK_SIZE);
    auto block1_hash = crypto::SHA1::compute(buf.data() + 0xC000, stfs::BLOCK_SIZE);

    // Build the hash table at offset 0xA000
    // Per formula: level1_hash_table_offset(0, 0x971A, 1) = 0xA000 + (0 << 12) = 0xA000
    auto* ht = buf.data() + 0xA000;
    // Entry 0 (block 0 = file table)
    std::memcpy(ht + 0 * 24, block0_hash.data(), 20);
    // info_raw (BE uint32): 0x80FFFFFF (state=kInUse, next=0xFFFFFF=end of chain)
    ht[0 * 24 + 20] = 0x80;
    ht[0 * 24 + 21] = 0xFF;
    ht[0 * 24 + 22] = 0xFF;
    ht[0 * 24 + 23] = 0xFF;
    // Entry 1 (block 1 = file data)
    std::memcpy(ht + 1 * 24, block1_hash.data(), 20);
    ht[1 * 24 + 20] = 0x80;
    ht[1 * 24 + 21] = 0xFF;
    ht[1 * 24 + 22] = 0xFF;
    ht[1 * 24 + 23] = 0xFF;

    // Write the file
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "Failed to open output file: " << out_path << "\n";
        return 1;
    }
    out.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    if (!out) {
        std::cerr << "Failed to write output file\n";
        return 1;
    }

    std::cout << "Created test STFS file: " << out_path << "\n";
    std::cout << "  Title ID:       " << str::format_hex_u32(title_id) << "\n";
    std::cout << "  Content Type:   " << str::format_hex_u32(content_type) << "\n";
    std::cout << "  File name:      " << filename << "\n";
    std::cout << "  File size:      " << filedata.size() << " bytes\n";
    std::cout << "  Total package:  " << buf.size() << " bytes\n";
    std::cout << "  Header size:    0x" << std::hex << HEADER_SIZE << std::dec << "\n";
    std::cout << "  Base offset:    0x" << std::hex << BASE_OFFSET << std::dec << "\n";
    return 0;
}
