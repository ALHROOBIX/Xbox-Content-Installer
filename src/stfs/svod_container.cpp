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
#include "xbox/stfs/svod_container.hpp"

#include "xbox/core/endian.hpp"
#include "xbox/core/errors.hpp"
#include "xbox/core/logger.hpp"
#include "xbox/utils/string_utils.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace xbox::stfs {

namespace fs = std::filesystem;

std::string SvodDeviceDescriptor::to_string() const {
    std::string out;
    out.reserve(256);
    out.append("  Descriptor Length:     ").append(std::to_string(descriptor_length)).append("\n");
    out.append("  Block Cache Elements:  ").append(std::to_string(block_cache_element_count)).append("\n");
    out.append("  Worker Thread Proc:    ").append(std::to_string(worker_thread_processor)).append("\n");
    out.append("  Worker Thread Prio:    ").append(std::to_string(worker_thread_priority)).append("\n");
    out.append("  First Fragment Hash:   ")
       .append(str::to_hex(first_fragment_hash_entry.data(), first_fragment_hash_entry.size())).append("\n");
    out.append("  Features Byte:         0x");
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02X", features_byte);
    out.append(buf);
    out.append(is_enhanced_gdf() ? " (EnhancedGDF)\n" : "\n");
    out.append("  Num Data Blocks:       ").append(std::to_string(num_data_blocks)).append("\n");
    out.append("  Start Data Block:      ").append(std::to_string(start_data_block)).append("\n");
    return out;
}

Result<SvodDeviceDescriptor, Error> parse_svod_descriptor(std::span<const byte> buffer) {
    if (buffer.size() < svod::vol::DESCRIPTOR_SIZE) {
        return XBOX_STFS_ERROR(InvalidVolumeDescriptor,
            "SVOD descriptor buffer too small: " + std::to_string(buffer.size()));
    }

    const auto* p = reinterpret_cast<const u8*>(buffer.data());
    SvodDeviceDescriptor d{};

    d.descriptor_length = p[svod::vol::DESCRIPTOR_LENGTH];
    d.block_cache_element_count = p[svod::vol::BLOCK_CACHE_ELEMENT_COUNT];
    d.worker_thread_processor = p[svod::vol::WORKER_THREAD_PROCESSOR];
    d.worker_thread_priority = p[svod::vol::WORKER_THREAD_PRIORITY];
    std::memcpy(d.first_fragment_hash_entry.data(),
                p + svod::vol::FIRST_FRAGMENT_HASH_ENTRY, 0x14);
    d.features_byte = p[svod::vol::FEATURES_BYTE];

    // 3-byte LE values
    d.num_data_blocks = static_cast<u32>(p[svod::vol::NUM_DATA_BLOCKS_RAW]) |
                        (static_cast<u32>(p[svod::vol::NUM_DATA_BLOCKS_RAW + 1]) << 8) |
                        (static_cast<u32>(p[svod::vol::NUM_DATA_BLOCKS_RAW + 2]) << 16);
    d.start_data_block = static_cast<u32>(p[svod::vol::START_DATA_BLOCK_RAW]) |
                         (static_cast<u32>(p[svod::vol::START_DATA_BLOCK_RAW + 1]) << 8) |
                         (static_cast<u32>(p[svod::vol::START_DATA_BLOCK_RAW + 2]) << 16);
    std::memcpy(d.reserved.data(), p + svod::vol::RESERVED, 5);

    return d;
}

Result<SvodDeviceDescriptor, Error> parse_svod_descriptor_from_header(
    std::span<const byte> header_buffer) {
    if (header_buffer.size() < stfs::off::VOLUME_DESCRIPTOR + svod::vol::DESCRIPTOR_SIZE) {
        return XBOX_STFS_ERROR(InvalidHeaderSize,
            "header buffer too small for SVOD descriptor");
    }
    return parse_svod_descriptor(
        std::span<const byte>(
            reinterpret_cast<const byte*>(header_buffer.data() + stfs::off::VOLUME_DESCRIPTOR),
            svod::vol::DESCRIPTOR_SIZE));
}

namespace {

// Read the magic string at the given offset from the first data file
Result<std::array<u8, 20>, Error> read_magic_at(const fs::path& file, u64 offset) {
    std::ifstream f(file, std::ios::binary);
    if (!f) {
        return XBOX_IO_ERROR(FileOpenFailed, "Failed to open: " + file.string());
    }
    f.seekg(static_cast<std::streamoff>(offset));
    if (!f) {
        return XBOX_IO_ERROR(FileSeekFailed, "Seek to " + std::to_string(offset) + " failed");
    }
    std::array<u8, 20> magic{};
    f.read(reinterpret_cast<char*>(magic.data()), 20);
    if (!f || f.gcount() != 20) {
        return XBOX_IO_ERROR(FileReadFailed, "Short read at offset " + std::to_string(offset));
    }
    return magic;
}

bool is_media_magic(const std::array<u8, 20>& m) noexcept {
    for (std::size_t i = 0; i < 20; ++i) {
        if (m[i] != static_cast<u8>(svod::MEDIA_MAGIC[i])) return false;
    }
    return true;
}

// Check for XSF magic at the given offset
bool is_xsf_magic(const std::array<u8, 20>& m) noexcept {
    // XSF magic is "XSF" followed by 17 zero bytes (or similar)
    return m[0] == 'X' && m[1] == 'S' && m[2] == 'F';
}

} // namespace

Result<SvodLayoutType, Error> detect_svod_layout(const fs::path& header_path) {
    // The data files live in a `.data` directory next to the header
    auto data_dir = header_path;
    data_dir += ".data";

    std::error_code ec;
    if (!fs::exists(data_dir, ec)) {
        return XBOX_STFS_ERROR(FileNotFound,
            "SVOD data directory not found: " + data_dir.string());
    }

    // Find the first data file (Data0000)
    auto first_file = data_dir / "Data0000";
    if (!fs::exists(first_file, ec)) {
        // Try lowercase
        first_file = data_dir / "data0000";
        if (!fs::exists(first_file, ec)) {
            return XBOX_STFS_ERROR(FileNotFound,
                "First SVOD data file not found in: " + data_dir.string());
        }
    }

    // Check for EGDF / MultipleFiles layout (magic at 0x2000)
    {
        auto magic = read_magic_at(first_file, 0x2000);
        if (magic.is_ok() && is_media_magic(magic.value())) {
            // Distinguish EGDF from MultipleFiles by checking the descriptor's
            // enhanced_gdf_layout bit. For now, return EnhancedGDF.
            return SvodLayoutType::EnhancedGDF;
        }
    }

    // Check for XSF layout (magic at 0x12000)
    {
        auto magic = read_magic_at(first_file, 0x12000);
        if (magic.is_ok() && is_xsf_magic(magic.value())) {
            return SvodLayoutType::XSF;
        }
    }

    // Check for SingleFile layout (magic at 0xD000)
    {
        auto magic = read_magic_at(first_file, 0xD000);
        if (magic.is_ok() && is_media_magic(magic.value())) {
            return SvodLayoutType::SingleFile;
        }
    }

    return SvodLayoutType::Unknown;
}

SvodBlockLocation svod_block_to_location(
    u32 block_index,
    u32 start_data_block,
    SvodLayoutType layout,
    u64 svod_base_offset) noexcept {

    SvodBlockLocation loc;

    // Per Xenia's BlockToOffset in svod_container_device.cc
    const u64 BLOCK_SIZE = svod::BLOCK_SIZE;
    const u64 HASH_BLOCK_SIZE = svod::HASH_BLOCK_SIZE;
    const u64 BLOCKS_PER_L0_HASH = svod::BLOCKS_PER_L0_HASH;
    const u64 HASHES_PER_L1_HASH = svod::HASHES_PER_L1_HASH;
    const u64 BLOCKS_PER_FILE = svod::BLOCKS_PER_FILE;
    const u64 MAX_FILE_SIZE = svod::MAX_FILE_SIZE;

    // Convert SVOD block index to "true block" by adjusting for start_data_block
    u64 true_block = static_cast<u64>(block_index) - (static_cast<u64>(start_data_block) * 2);
    if (layout == SvodLayoutType::EnhancedGDF) {
        true_block += 0x2;  // EGDF has a 0x1000 byte offset
    }

    u64 file_block = true_block % BLOCKS_PER_FILE;
    u64 file_index = true_block / BLOCKS_PER_FILE;
    u64 offset = 0;

    // Add offset for Level-0 hash tables
    u64 level0_table_count = (file_block / BLOCKS_PER_L0_HASH) + 1;
    offset += level0_table_count * HASH_BLOCK_SIZE;

    // Add offset for Level-1 hash tables
    u64 level1_table_count = (level0_table_count / HASHES_PER_L1_HASH) + 1;
    offset += level1_table_count * HASH_BLOCK_SIZE;

    if (layout == SvodLayoutType::SingleFile) {
        offset += svod_base_offset;  // typically 0xB000
    }

    u64 block_address = (file_block * BLOCK_SIZE) + offset;

    // Handle overflow to next file
    if (block_address >= MAX_FILE_SIZE) {
        file_index += 1;
        block_address %= MAX_FILE_SIZE;
        block_address += 0x2000;
    }

    loc.file_index = static_cast<std::size_t>(file_index);
    loc.byte_offset = block_address;
    return loc;
}

std::string SvodDirectoryEntry::format() const {
    std::string out;
    out.reserve(128);
    out.append(is_directory() ? "[DIR] " : "[FILE] ");
    out.append(name);
    if (!is_directory()) {
        out.append(" (").append(std::to_string(length)).append(" bytes)");
    }
    out.append(" node_l=").append(std::to_string(node_l));
    out.append(" node_r=").append(std::to_string(node_r));
    out.append(" data_block=").append(std::to_string(data_block));
    return out;
}

Result<std::pair<SvodDirectoryEntry, std::size_t>, Error>
parse_svod_directory_entry(const u8* data, std::size_t max_len) {
    if (max_len < svod::DIR_ENTRY_SIZE) {
        return XBOX_STFS_ERROR(InvalidFileEntry,
            "buffer too small for SVOD directory entry");
    }

    SvodDirectoryEntry e;
    e.node_l = endian::read_u16_le(data);
    e.node_r = endian::read_u16_le(data + 2);
    e.data_block = endian::read_u32_be(data + 4);
    e.length = endian::read_u32_be(data + 8);
    e.attributes = data[12];
    e.name_length = data[13];

    if (e.name_length == 0 || e.name_length > 255) {
        return XBOX_STFS_ERROR(InvalidFileEntry,
            "invalid SVOD entry name length: " + std::to_string(e.name_length));
    }

    if (max_len < svod::DIR_ENTRY_SIZE + e.name_length) {
        return XBOX_STFS_ERROR(InvalidFileEntry,
            "buffer too small for SVOD entry name");
    }

    e.name.assign(reinterpret_cast<const char*>(data + svod::DIR_ENTRY_SIZE), e.name_length);

    std::size_t consumed = svod::DIR_ENTRY_SIZE + e.name_length;
    return std::make_pair(std::move(e), consumed);
}

Result<std::vector<fs::path>, Error> list_svod_data_files(const fs::path& header_path) {
    std::vector<fs::path> files;

    auto data_dir = header_path;
    data_dir += ".data";

    std::error_code ec;
    if (!fs::exists(data_dir, ec)) {
        return XBOX_IO_ERROR(FileNotFound,
            "SVOD data directory not found: " + data_dir.string());
    }

    // Collect all files matching the pattern "Data####" (case-insensitive)
    std::vector<std::string> filenames;
    for (auto& entry : fs::directory_iterator(data_dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        auto name = entry.path().filename().string();
        // Accept "Data0000" through "Data9999" (case-insensitive)
        std::string lower = str::to_lower(name);
        if (lower.size() == 8 && lower.substr(0, 4) == "data" &&
            std::all_of(lower.begin() + 4, lower.end(), [](char c) {
                return c >= '0' && c <= '9';
            })) {
            filenames.push_back(name);
        }
    }

    // Sort alphabetically (which also gives numeric order since names are
    // zero-padded to 4 digits)
    std::sort(filenames.begin(), filenames.end());

    for (const auto& name : filenames) {
        files.push_back(data_dir / name);
    }

    return files;
}

Result<SvodPackageInfo, Error> read_svod_info(const fs::path& header_path) {
    SvodPackageInfo info{};

    // Read the header file
    std::ifstream hf(header_path, std::ios::binary);
    if (!hf) {
        return XBOX_IO_ERROR(FileOpenFailed, "Failed to open header: " + header_path.string());
    }

    std::vector<u8> header_buf(stfs::HEADER_SIZE);
    hf.read(reinterpret_cast<char*>(header_buf.data()), stfs::HEADER_SIZE);
    if (!hf || hf.gcount() < static_cast<std::streamsize>(stfs::HEADER_SIZE)) {
        return XBOX_IO_ERROR(FileReadFailed, "Short header read");
    }

    // Parse the SVOD device descriptor
    SvodDeviceDescriptor desc;
    XBOX_TRY_ASSIGN(desc, parse_svod_descriptor_from_header(
        std::span<const byte>(reinterpret_cast<const byte*>(header_buf.data()),
                              stfs::HEADER_SIZE)));
    info.descriptor = desc;

    // Detect the layout type
    SvodLayoutType layout;
    XBOX_TRY_ASSIGN(layout, detect_svod_layout(header_path));
    info.layout = layout;

    // List the data files
    std::vector<fs::path> data_files;
    XBOX_TRY_ASSIGN(data_files, list_svod_data_files(header_path));
    info.data_files = std::move(data_files);

    // Compute total data size
    info.total_data_size = 0;
    for (const auto& f : info.data_files) {
        std::error_code ec;
        auto sz = fs::file_size(f, ec);
        if (!ec) {
            info.total_data_size += sz;
        }
    }

    // Set the base offset based on layout
    switch (layout) {
        case SvodLayoutType::EnhancedGDF:
            info.svod_base_offset = 0x0000;
            break;
        case SvodLayoutType::XSF:
            info.svod_base_offset = 0x10000;
            break;
        case SvodLayoutType::SingleFile:
            info.svod_base_offset = 0xB000;
            break;
        case SvodLayoutType::MultipleFiles:
            info.svod_base_offset = 0x0000;
            break;
        default:
            info.svod_base_offset = 0;
            break;
    }

    return info;
}

} // namespace xbox::stfs
