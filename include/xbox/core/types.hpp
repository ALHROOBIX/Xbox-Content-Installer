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

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xbox {

// ----- Integer type aliases (explicit width) -----
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using byte = std::byte;

// ----- STFS format constants -----
namespace stfs {

// Total length of the STFS header + metadata block (bytes).
// Per spec: header + metadata + thumbnails = 0x971A bytes.
constexpr u64 HEADER_SIZE = 0x971A;

// Magic values for the three STFS sub-formats.
// Note: "CON " has a trailing space (4 ASCII chars).
constexpr std::array<byte, 4> MAGIC_CON  = {byte('C'), byte('O'), byte('N'), byte(' ')};
constexpr std::array<byte, 4> MAGIC_PIRS = {byte('P'), byte('I'), byte('R'), byte('S')};
constexpr std::array<byte, 4> MAGIC_LIVE = {byte('L'), byte('I'), byte('V'), byte('E')};

// Critical field offsets inside the STFS metadata region.
// All offsets are absolute (measured from the start of the file).
namespace off {
    constexpr u64 MAGIC              = 0x000;
    constexpr u64 PACKAGE_SIGNATURE  = 0x004;  // LIVE/PIRS only (0x100 bytes)
    constexpr u64 LICENSE_ENTRIES    = 0x22C;  // 0x100 bytes (license data)
    constexpr u64 CONTENT_ID         = 0x32C;  // 0x14 bytes (also header SHA1)
    constexpr u64 ENTRY_ID           = 0x340;  // 4 bytes - controls hash-table layout
    constexpr u64 CONTENT_TYPE       = 0x344;  // 4 bytes signed int
    constexpr u64 METADATA_VERSION   = 0x348;  // 4 bytes signed int (1 or 2)
    constexpr u64 CONTENT_SIZE       = 0x34C;  // 8 bytes signed long
    constexpr u64 MEDIA_ID           = 0x354;  // 4 bytes unsigned int
    constexpr u64 VERSION            = 0x358;  // 4 bytes - title update version
    constexpr u64 BASE_VERSION       = 0x35C;  // 4 bytes - base version
    constexpr u64 TITLE_ID           = 0x360;  // 4 bytes unsigned int (BIG-ENDIAN)
    constexpr u64 PLATFORM           = 0x364;  // 1 byte
    constexpr u64 EXEC_TYPE          = 0x365;  // 1 byte
    constexpr u64 DISC_NUMBER        = 0x366;  // 1 byte
    constexpr u64 DISC_IN_SET        = 0x367;  // 1 byte
    constexpr u64 SAVE_GAME_ID       = 0x368;  // 4 bytes
    constexpr u64 CONSOLE_ID         = 0x36C;  // 5 bytes
    constexpr u64 PROFILE_ID         = 0x371;  // 8 bytes
    constexpr u64 VOL_DESC_SIZE      = 0x379;  // 1 byte (usually 0x24)
    constexpr u64 VOLUME_DESCRIPTOR  = 0x37A;  // 0x24 bytes
    constexpr u64 DATA_FILE_COUNT    = 0x39D;  // 4 bytes signed int
    constexpr u64 DATA_FILE_COMBINED = 0x3A1;  // 8 bytes signed long
    constexpr u64 DEVICE_ID          = 0x3FD;  // 0x14 bytes
    constexpr u64 DISPLAY_NAME       = 0x411;  // 0x900 bytes (12 locales * 0x80 UTF-16 BE)
    constexpr u64 DISPLAY_DESC       = 0xD11;  // 0x900 bytes
    constexpr u64 PUBLISHER_NAME     = 0x1611; // 0x80 bytes UTF-16 BE
    constexpr u64 TITLE_NAME         = 0x1691; // 0x80 bytes UTF-16 BE
    constexpr u64 TRANSFER_FLAGS     = 0x1711; // 1 byte
    constexpr u64 THUMBNAIL_SIZE     = 0x1712; // 4 bytes signed int
    constexpr u64 TITLE_THUMB_SIZE   = 0x1716; // 4 bytes signed int
    constexpr u64 THUMBNAIL_IMAGE    = 0x171A; // 0x4000 bytes (v1)
    constexpr u64 TITLE_THUMB_IMAGE  = 0x571A; // 0x4000 bytes (v1)
} // namespace off

// Volume descriptor offsets (relative to start of descriptor at 0x37A)
// Per Free60 STFS spec and verified against real Xbox 360 files.
// NOTE: file_table_block_count is LITTLE-ENDIAN (not BE as one might expect)!
namespace vol {
    constexpr u64 RESERVED                 = 0x00; // 1 byte
    constexpr u64 BLOCK_SEPARATION         = 0x01; // 1 byte (also called "version" by Xenia)
    constexpr u64 FILE_TABLE_BLOCK_COUNT   = 0x02; // 2 bytes LE (NOT BE!)
    constexpr u64 FILE_TABLE_BLOCK_NUMBER  = 0x04; // 3 bytes LE
    constexpr u64 TOP_HASH_TABLE_HASH      = 0x07; // 0x14 bytes SHA1
    constexpr u64 TOTAL_ALLOCATED_BLOCKS   = 0x1B; // 4 bytes BE
    constexpr u64 TOTAL_UNALLOCATED_BLOCKS = 0x1F; // 4 bytes BE
    constexpr u64 DESCRIPTOR_SIZE          = 0x24; // Total volume descriptor size
} // namespace vol

// ----- Block & hash-table geometry -----
// Per Xenia's stfs_container_device.h
constexpr u64 BLOCK_SIZE             = 0x1000;        // 4096 bytes (kBlockSize)
constexpr u64 HASH_TABLE_BLOCKS_STEP = 0xAA;          // 170 - one hash table every 0xAA data blocks
constexpr u64 LEVEL_2_STEP           = 0x70E4;        // 0xAA * 0xAA = 28900
constexpr u64 LEVEL_3_STEP           = 0x4AF768;      // 0xAA * 0xAA * 0xAA = 4913000

// Number of hash levels in STFS (per Xenia: kBlocksHashLevelAmount = 3)
constexpr u32 BLOCKS_HASH_LEVEL_AMOUNT = 3;

// Blocks per hash level (per Xenia: kBlocksPerHashLevel)
// Level 0: 170 blocks, Level 1: 28900, Level 2: 4913000
constexpr u32 BLOCKS_PER_HASH_LEVEL[BLOCKS_HASH_LEVEL_AMOUNT] = {170, 28900, 4913000};

// End of chain marker (per Xenia: kEndOfChain)
constexpr u32 END_OF_CHAIN = 0xFFFFFF;

// Number of directory entries per 0x1000-byte block (0x1000 / 0x40 = 64)
constexpr u32 ENTRIES_PER_DIRECTORY_BLOCK = 64;

// Hash-table entry size: 20 bytes SHA1 + 1 status + 3 next-block index = 0x18
constexpr u64 HASH_ENTRY_SIZE        = 0x18;
constexpr u64 HASH_TABLE_SIZE        = HASH_TABLE_BLOCKS_STEP * HASH_ENTRY_SIZE; // 170 * 0x18 = 0x1788

// Hash status byte values are now defined in stfs_hash.hpp (not here, to avoid
// redefinition since stfs_hash.hpp includes types.hpp)

// ----- File listing (directory entry) geometry -----
constexpr u64 FILE_ENTRY_SIZE        = 0x40;  // 64 bytes per entry

// Mask to detect "two hash tables per cluster" layout.
// Per spec: if ((Entry ID + 0xFFF) & 0xF000) >> 0xC == 0xB, two tables exist.
constexpr u32 TWO_TABLES_MASK        = 0xB000;
constexpr u32 TWO_TABLES_TEST_VALUE  = 0xB000;

// Magic offset of the first data block (after header + first hash table + file table).
// Per spec, the calculation is: header_size + first_hash_table + file_table_blocks.
// In practice the first data block starts at 0xC000 (0x971A header rounded up + tables).
// We compute the actual offset dynamically via block_to_offset(); this constant is used
// only as a sanity-check lower bound.
constexpr u64 FIRST_DATA_BLOCK_OFFSET_MIN = 0xA000;

} // namespace stfs

// ----- Content type catalog (subset of STFS Content Types table) -----
namespace content_type {
    // These are the canonical 32-bit content type codes used by Xbox 360.
    // We use them for path resolution: e.g. DLC -> 00000002, TU -> 000B0000.
    constexpr u32 SAVED_GAME           = 0x00000001;
    constexpr u32 MARKETPLACE_CONTENT  = 0x00000002;  // DLC / marketplace items
    constexpr u32 PUBLISHER            = 0x00000003;
    constexpr u32 IPTV_PAUSE_BUFFER    = 0x00002000;
    constexpr u32 XBOX_360_TITLE       = 0x00001000;
    constexpr u32 XBOX_ORIGINAL_GAME   = 0x00005000;
    constexpr u32 INSTALLED_GAME       = 0x00004000;
    constexpr u32 GAMER_PICTURE        = 0x00020000;
    constexpr u32 THEME                = 0x00030000;
    constexpr u32 CACHE_FILE           = 0x00040000;
    constexpr u32 STORAGE_DOWNLOAD     = 0x00050000;
    constexpr u32 XBOX_SAVED_GAME      = 0x00060000;
    constexpr u32 XBOX_DOWNLOAD        = 0x00070000;
    constexpr u32 GAME_DEMO            = 0x00080000;
    constexpr u32 AVATAR_ITEM          = 0x00009000;
    constexpr u32 VIDEO                = 0x00090000;
    constexpr u32 GAME_TITLE           = 0x000A0000;
    constexpr u32 ARCADE_TITLE         = 0x000D0000;
    constexpr u32 GAME_TRAILER         = 0x000C0000;
    constexpr u32 TITLE_UPDATE         = 0x000B0000;  // <-- TU specifically (also called Installer)
    // INSTALLER is the same value as TITLE_UPDATE - they share the 0x000B0000 slot
    constexpr u32 LICENSE_STORE        = 0x000F0000;
    constexpr u32 MOVIE                = 0x00100000;
    constexpr u32 GAME_VIDEO           = 0x00400000;
    constexpr u32 TV                   = 0x00200000;
    constexpr u32 MUSIC_VIDEO          = 0x00300000;
    constexpr u32 PODCAST_VIDEO        = 0x00500000;
    constexpr u32 VIRAL_VIDEO          = 0x00600000;
    constexpr u32 COMMUNITY_GAME       = 0x02000000;
    constexpr u32 XNA                  = 0x000E0000;
} // namespace content_type

// ----- Path layout constants (Xenia-canary style) -----
namespace paths {
    // Root content directory under the install prefix.
    constexpr const char* CONTENT_ROOT_DIR = "content";

    // Subdirectory inside content type folder for the actual content.
    // Xenia-canary uses 00000002 as the "Content" subdirectory under each
    // content-type folder (matches FATX Content.meta.xsl convention).
    constexpr const char* CONTENT_SUBDIR = "00000002";

    // Suffix used when disabling content (renamed directory).
    constexpr const char* DISABLED_SUFFIX = ".disabled";
} // namespace paths

// ----- License entry layout (informational; not used for verification) -----
namespace license {
    constexpr u64 ENTRY_SIZE  = 0x10;
    constexpr u64 ENTRY_COUNT = 0x10; // 16 license entries

    // Per Xenia's stfs_xbox.h: XContentLicense struct
    //   be<uint64_t> licensee_id;    // +0x00 (8 bytes)
    //   be<uint32_t> license_bits;   // +0x08 (4 bytes)
    //   be<uint32_t> license_flags;  // +0x0C (4 bytes)
    constexpr u64 LICENSEE_ID_OFFSET   = 0x00;
    constexpr u64 LICENSE_BITS_OFFSET  = 0x08;
    constexpr u64 LICENSE_FLAGS_OFFSET = 0x0C;
} // namespace license

// ----- Volume type (read at offset 0x3A9) -----
// Per Xenia's stfs_xbox.h: XContentVolumeType
//   0 = STFS (single-file container)
//   1 = SVOD (multi-file container, used for large DLC / GOD containers)
enum class VolumeType : u32 {
    Stfs = 0,
    Svod = 1,
    Unknown = 0xFFFFFFFF,
};

// ----- SVOD layout types (from Xenia's svod_container_device.h) -----
enum class SvodLayoutType : u8 {
    Unknown        = 0x0,
    EnhancedGDF    = 0x1,  // EGDF magic at 0x2000
    XSF            = 0x2,  // XSF magic at 0x12000
    SingleFile     = 0x4,  // Single file - magic at 0xD000
    MultipleFiles  = 0x8,  // Multiple files - magic at 0x2000
};

// SVOD block geometry (per Xenia's svod_container_device.cc)
namespace svod {
    constexpr u64 BLOCK_SIZE           = 0x800;     // 2 KB
    constexpr u64 HASH_BLOCK_SIZE      = 0x1000;    // 4 KB hash tables
    constexpr u64 BLOCKS_PER_L0_HASH   = 0x198;     // 408 blocks per L0 hash
    constexpr u64 HASHES_PER_L1_HASH   = 0xA1C4;    // 41412 L0 hashes per L1
    constexpr u64 BLOCKS_PER_FILE      = 0x14388;   // 82824 blocks per file
    constexpr u64 MAX_FILE_SIZE        = 0xA290000; // ~170 MB per file

    // SVOD magic string for EGDF / Multi-file layouts
    constexpr std::array<char, 20> MEDIA_MAGIC = {
        'M', 'I', 'C', 'R', 'O', 'S', 'O', 'F', 'T', '*',
        'X', 'B', 'O', 'X', '*', 'M', 'E', 'D', 'I', 'A'
    };

    // Directory entry size in SVOD (binary tree node)
    constexpr u64 DIR_ENTRY_SIZE = 0xE;

    // Offsets within the SVOD device descriptor (at 0x37A in header, same as STFS)
    namespace vol {
        constexpr u64 DESCRIPTOR_LENGTH        = 0x00;
        constexpr u64 BLOCK_CACHE_ELEMENT_COUNT = 0x01;
        constexpr u64 WORKER_THREAD_PROCESSOR  = 0x02;
        constexpr u64 WORKER_THREAD_PRIORITY   = 0x03;
        constexpr u64 FIRST_FRAGMENT_HASH_ENTRY = 0x04; // 20 bytes
        constexpr u64 FEATURES_BYTE            = 0x18;
        constexpr u64 NUM_DATA_BLOCKS_RAW      = 0x19; // 3 bytes
        constexpr u64 START_DATA_BLOCK_RAW     = 0x1C; // 3 bytes
        constexpr u64 RESERVED                 = 0x1F;
        constexpr u64 DESCRIPTOR_SIZE          = 0x24;
    } // namespace vol
} // namespace svod

// ----- Additional STFS offsets discovered from Xenia source -----
namespace stfs {
    // Volume type field (4 bytes BE) - distinguishes STFS vs SVOD
    constexpr u64 VOLUME_TYPE          = 0x3A9;
    // Online creator (8 bytes BE) - present in metadata v2
    constexpr u64 ONLINE_CREATOR       = 0x3AD;
    // Category (4 bytes BE)
    constexpr u64 CATEGORY             = 0x3B5;

    // XContentVolumeType values
    constexpr u32 VOLUME_TYPE_STFS = 0;
    constexpr u32 VOLUME_TYPE_SVOD = 1;

    // Header size field at 0x340 (4 bytes BE) - typically 0x971A
    // Used by Xenia to round_up to kBlockSize (0x1000) for hash table offset
    constexpr u64 HEADER_SIZE_FIELD   = 0x340;

    // Block size used for STFS layout
    constexpr u64 METADATA_BLOCK_SIZE = 0x1000;
} // namespace stfs

} // namespace xbox
