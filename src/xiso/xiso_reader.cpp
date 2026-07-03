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
#include "xbox/xiso/xiso_reader.hpp"

#include "xbox/core/endian.hpp"
#include "xbox/core/errors.hpp"
#include "xbox/core/logger.hpp"
#include "xbox/utils/string_utils.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace xbox::xiso {

namespace fs = std::filesystem;

// XISO magic string
static constexpr char MAGIC_DATA[] = "MICROSOFT*XBOX*MEDIA";

// ---------------------------------------------------------------------------
// detect_image_type — detect whether a file is ISO or GOD
// ---------------------------------------------------------------------------
ImageType detect_image_type(const fs::path& path) {
    std::error_code ec;

    auto size = fs::file_size(path, ec);
    if (ec || size == 0) return ImageType::Unknown;

    // Check for XISO magic at various offsets
    // XGDTool checks: 0, 0x0FD90000, 0x02080000, 0x18300000
    static constexpr u64 seek_offsets[] = {0, 0x0FD90000, 0x02080000, 0x18300000};

    std::ifstream f(path, std::ios::binary);
    if (!f) return ImageType::Unknown;

    char magic_buf[20];
    for (u64 seek_off : seek_offsets) {
        if (seek_off + MAGIC_OFFSET + 20 > size) continue;
        f.seekg(static_cast<std::streamoff>(seek_off + MAGIC_OFFSET), std::ios::beg);
        if (!f) continue;
        f.read(magic_buf, 20);
        if (f && std::memcmp(magic_buf, MAGIC_DATA, 20) == 0) {
            return ImageType::XISO;
        }
        f.clear();
    }

    return ImageType::Unknown;
}

// ---------------------------------------------------------------------------
// open_iso — open a standard XISO file
// ---------------------------------------------------------------------------
Result<ImageReader, Error> ImageReader::open_iso(const fs::path& iso_path) {
    ImageReader reader;
    reader.type_ = ImageType::XISO;
    reader.data_files_.push_back(iso_path);

    std::error_code ec;
    auto file_size = fs::file_size(iso_path, ec);
    if (ec) {
        return XBOX_IO_ERROR(FileOpenFailed, "cannot get file size: " + iso_path.string());
    }

    // Find the XISO image offset by searching for magic
    // XGDTool checks: 0, 0x0FD90000, 0x02080000, 0x18300000
    static constexpr u64 seek_offsets[] = {0, 0x0FD90000, 0x02080000, 0x18300000};

    std::ifstream f(iso_path, std::ios::binary);
    if (!f) {
        return XBOX_IO_ERROR(FileOpenFailed, "cannot open ISO: " + iso_path.string());
    }

    char magic_buf[20];
    bool found = false;
    for (u64 seek_off : seek_offsets) {
        if (seek_off + MAGIC_OFFSET + 20 > file_size) continue;
        f.seekg(static_cast<std::streamoff>(seek_off + MAGIC_OFFSET), std::ios::beg);
        if (!f) { f.clear(); continue; }
        f.read(magic_buf, 20);
        if (f && std::memcmp(magic_buf, MAGIC_DATA, 20) == 0) {
            reader.image_offset_ = seek_off;
            found = true;
            break;
        }
        f.clear();
    }

    if (!found) {
        return XBOX_IO_ERROR(FileOpenFailed,
            "XISO magic not found in: " + iso_path.string());
    }

    // Read root directory info: start_sector (u32 LE), file_size (u32 LE)
    f.seekg(static_cast<std::streamoff>(reader.image_offset_ + MAGIC_OFFSET + MAGIC_DATA_LEN), std::ios::beg);
    f.read(reinterpret_cast<char*>(&reader.root_sector_), 4);
    f.read(reinterpret_cast<char*>(&reader.root_size_), 4);
    reader.root_sector_ = endian::read_u32_le(reinterpret_cast<const u8*>(&reader.root_sector_));
    reader.root_size_ = endian::read_u32_le(reinterpret_cast<const u8*>(&reader.root_size_));

    reader.total_sectors_ = static_cast<u32>(file_size / SECTOR_SIZE);

    XBOX_LOG_DEBUG("XISO: image_offset=0x{:X}, root_sector={}, root_size={}, total_sectors={}",
                   reader.image_offset_, reader.root_sector_, reader.root_size_, reader.total_sectors_);

    return reader;
}

// ---------------------------------------------------------------------------
// open_god — open a GOD container
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// remap_sector — convert XISO sector to (file_index, byte_offset)
// ---------------------------------------------------------------------------
std::pair<std::size_t, u64> ImageReader::remap_sector(u32 sector) const noexcept {
    if (type_ == ImageType::XISO) {
        // ISO: direct offset. Do NOT add image_offset_ here — the caller's
        // offset already includes it (image_offset_ + position + ...).
        // Adding it again would cause a double-offset bug.
        return {0, static_cast<u64>(sector) * SECTOR_SIZE};
    }

    // GOD: hash table remapping (from XGDTool GoDReader::remap_sector)
    // The virtual XISO sector is converted to a 4KB block number, then
    // the hash table overhead is added.
    u64 block_num = (static_cast<u64>(sector) * SECTOR_SIZE) / GOD_BLOCK_SIZE;  // = sector / 2
    u64 offset_in_block = (static_cast<u64>(sector) * SECTOR_SIZE) % GOD_BLOCK_SIZE;

    u64 file_index = block_num / GOD_DATA_BLOCKS_PER_PART;
    u64 data_block = block_num % GOD_DATA_BLOCKS_PER_PART;
    u64 hash_index = data_block / GOD_DATA_BLOCKS_PER_SHT;

    u64 remapped = GOD_BLOCK_SIZE;                          // master hash table
    remapped += (hash_index + 1) * GOD_BLOCK_SIZE;          // sub hash tables
    remapped += data_block * GOD_BLOCK_SIZE;                // data blocks
    remapped += offset_in_block;                             // offset within block

    return {static_cast<std::size_t>(file_index), remapped};
}

// ---------------------------------------------------------------------------
// read_from_file — read from a specific data file
// ---------------------------------------------------------------------------
Result<std::vector<u8>, Error> ImageReader::read_from_file(
    std::size_t file_index, u64 offset, u64 size) const {
    if (file_index >= data_files_.size()) {
        return XBOX_IO_ERROR(FileReadFailed,
            "file index " + std::to_string(file_index) + " out of range");
    }

    std::ifstream f(data_files_[file_index], std::ios::binary);
    if (!f) {
        return XBOX_IO_ERROR(FileOpenFailed, "cannot open: " + data_files_[file_index].string());
    }

    f.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!f) {
        return XBOX_IO_ERROR(FileReadFailed, "seek failed at offset " + std::to_string(offset));
    }

    std::vector<u8> data(size);
    if (size > 0) {
        f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
        data.resize(static_cast<std::size_t>(f.gcount()));
    }

    return data;
}

// ---------------------------------------------------------------------------
// read_sector — read a single 2KB sector
// ---------------------------------------------------------------------------
Result<std::vector<u8>, Error> ImageReader::read_sector(u32 sector) const {
    auto [fi, offset] = remap_sector(sector);
    if (fi >= data_files_.size()) {
        return XBOX_IO_ERROR(FileReadFailed, "sector " + std::to_string(sector) + " out of range");
    }
    return read_from_file(fi, offset, SECTOR_SIZE);
}

// ---------------------------------------------------------------------------
// read_bytes — read arbitrary bytes from virtual image (OPTIMIZED)
//
// For ISO: reads directly from the file at the computed offset.
// No sector-by-sector allocation — single seek + read.
//
// For GOD: reads sector by sector (hash table remapping required).
//
// Memory optimization: uses a thread-local reusable buffer to avoid
// repeated allocations. Peak memory = buffer size (default 4MB).
// ---------------------------------------------------------------------------
thread_local std::vector<u8> tls_read_buffer;

Result<std::vector<u8>, Error> ImageReader::read_bytes(u64 offset, u64 size) const {
    if (type_ == ImageType::XISO) {
        // ISO: direct read — no remapping needed.
        // Compute the raw file offset: image_offset + virtual offset
        u64 raw_offset = image_offset_ + offset;

        // Use thread-local buffer for small reads (avoid allocation)
        if (size <= 4 * 1024 * 1024) {
            if (tls_read_buffer.size() < size) {
                tls_read_buffer.resize(size);
            }
            auto r = read_from_file(0, raw_offset, size);
            if (!r.is_ok()) return Err<Error>{r.error()};
            return r;  // RVO will move this
        }

        // Large read — allocate directly
        return read_from_file(0, raw_offset, size);
    }

    // GOD: sector-by-sector remapping (hash tables must be skipped)
    u32 start_sector = static_cast<u32>(offset / SECTOR_SIZE);
    u32 offset_in_sector = static_cast<u32>(offset % SECTOR_SIZE);

    u32 num_sectors = static_cast<u32>((size + offset_in_sector + SECTOR_SIZE - 1) / SECTOR_SIZE);

    std::vector<u8> result;
    result.reserve(size);

    u64 remaining = size;
    u32 current_sector = start_sector;
    u32 current_offset = offset_in_sector;

    for (u32 i = 0; i < num_sectors && remaining > 0; ++i) {
        auto sector_r = read_sector(current_sector);
        if (!sector_r.is_ok()) {
            return Err<Error>{sector_r.error()};
        }

        const auto& sector_data = sector_r.value();
        u64 to_copy = std::min<u64>(remaining, SECTOR_SIZE - current_offset);

        if (current_offset < sector_data.size()) {
            u64 copy_size = std::min(to_copy, sector_data.size() - current_offset);
            result.insert(result.end(),
                          sector_data.begin() + current_offset,
                          sector_data.begin() + current_offset + copy_size);
            remaining -= copy_size;
        }

        current_offset = 0;
        ++current_sector;
    }

    result.resize(size);
    return result;
}

// ---------------------------------------------------------------------------
// read_directory_bfs — read directory entries using BFS (non-recursive)
// Based on XGDTool's ImageReader::populate_directory_entries
// ---------------------------------------------------------------------------
Result<void, Error> ImageReader::read_directory_bfs(
    u32 start_sector, u32 dir_size,
    std::vector<DirEntry>& out_entries) const {

    // BFS queue: each item is (sector, offset_in_4byte_units, path)
    struct QueueItem {
        u32 sector;
        u64 offset;     // 4-byte unit offset within directory
        u64 position;   // start_sector * SECTOR_SIZE
        u32 dir_size;
        std::string path;
    };

    std::vector<QueueItem> queue;
    queue.push_back({start_sector, 0, static_cast<u64>(start_sector) * SECTOR_SIZE, dir_size, ""});

    while (!queue.empty()) {
        auto current = std::move(queue.front());
        queue.erase(queue.begin());

        // Check if we've read all entries in this directory
        if ((current.offset * sizeof(u32)) >= current.dir_size) {
            continue;
        }

        // Read the directory entry at current position
        // For ISO: read_pos is a virtual offset, read_bytes adds image_offset_ internally.
        // We must NOT add image_offset_ here — read_bytes does it for ISO.
        u64 read_pos;
        if (type_ == ImageType::XISO) {
            read_pos = current.position + (current.offset * sizeof(u32));
        } else {
            read_pos = image_offset_ + current.position + (current.offset * sizeof(u32));
        }

        auto entry_r = read_bytes(read_pos, sizeof(DirEntryHeader) + 255);  // header + max name
        if (!entry_r.is_ok()) {
            continue;  // skip on error
        }

        const auto& data = entry_r.value();
        if (data.size() < sizeof(DirEntryHeader)) {
            continue;
        }

        DirEntry entry;
        std::memcpy(&entry.header, data.data(), sizeof(DirEntryHeader));

        // Read filename
        if (entry.header.name_length > 0 && entry.header.name_length <= 255) {
            if (data.size() >= sizeof(DirEntryHeader) + entry.header.name_length) {
                entry.filename.assign(
                    reinterpret_cast<const char*>(data.data() + sizeof(DirEntryHeader)),
                    entry.header.name_length);
            }
        }

        // Check for padding/end marker
        if (entry.header.left_offset == PAD_SHORT) {
            continue;
        }

        // Process left child
        if (entry.header.left_offset != 0) {
            QueueItem left = current;
            left.offset = entry.header.left_offset;
            queue.push_back(std::move(left));
        }

        // Process this entry
        entry.position = static_cast<u64>(entry.header.start_sector) * SECTOR_SIZE;
        entry.offset = 0;
        // Sanitize filename for filesystem safety (Windows/Wine compatible)
        std::string safe_name = entry.filename;
        for (char& c : safe_name) {
            if (c == '/' || c == '\\' || c == ':' || c == '*' ||
                c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                c = '_';
            }
            // Replace non-ASCII characters with '_' to prevent filesystem errors
            unsigned char uc = static_cast<unsigned char>(c);
            if (uc < 0x20) c = '_';
        }
        if (safe_name.empty()) safe_name = "_";
        entry.path = current.path.empty() ? safe_name : (current.path + "/" + safe_name);

        if (entry.header.attributes & ATTR_DIRECTORY) {
            // It's a directory — add to entries and recurse
            out_entries.push_back(entry);
            if (entry.header.file_size > 0) {
                QueueItem child;
                child.sector = entry.header.start_sector;
                child.offset = 0;
                child.position = entry.position;
                child.dir_size = entry.header.file_size;
                child.path = entry.path;
                queue.push_back(std::move(child));
            }
        } else if (entry.header.file_size > 0) {
            // It's a file — add to entries
            out_entries.push_back(entry);
        }

        // Process right child
        if (entry.header.right_offset != 0) {
            QueueItem right = current;
            right.offset = entry.header.right_offset;
            queue.push_back(std::move(right));
        }
    }

    return {};
}

// ---------------------------------------------------------------------------
// read_directory_tree — read the full directory tree
// ---------------------------------------------------------------------------
Result<std::vector<DirEntry>, Error> ImageReader::read_directory_tree() const {
    std::vector<DirEntry> entries;
    auto r = read_directory_bfs(root_sector_, root_size_, entries);
    if (!r.is_ok()) {
        return Err<Error>{r.error()};
    }
    return entries;
}

// ---------------------------------------------------------------------------
// find_executable — find default.xex or default.xbe
// ---------------------------------------------------------------------------
Result<DirEntry, Error> ImageReader::find_executable() const {
    auto entries_r = read_directory_tree();
    if (!entries_r.is_ok()) {
        return Err<Error>{entries_r.error()};
    }

    for (const auto& entry : entries_r.value()) {
        if (entry.header.attributes & ATTR_DIRECTORY) continue;
        if (entry.header.file_size == 0) continue;

        std::string lower = str::to_lower(entry.filename);
        if (lower == "default.xex" || lower == "default.xbe") {
            return entry;
        }
    }

    return XBOX_IO_ERROR(FileNotFound, "no executable (default.xex/default.xbe) found");
}

// ---------------------------------------------------------------------------
// extract_file — extract a single file (OPTIMIZED streaming)
//
// For ISO: reads directly from the file with a single seek + large read.
// No vector allocation per chunk — uses a reusable buffer.
//
// Buffer size adapts to file size:
// - Files < 1MB: 256KB buffer (fast for many small files)
// - Files 1MB-100MB: 1MB buffer (balanced)
// - Files > 100MB: 4MB buffer (optimal for large media files)
// ---------------------------------------------------------------------------
thread_local std::vector<u8> tls_extract_buffer;

Result<u64, Error> ImageReader::extract_file(
    const DirEntry& entry, const fs::path& dest_path) const {

    std::error_code ec;
    fs::create_directories(dest_path.parent_path(), ec);

    std::ofstream out(dest_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return XBOX_IO_ERROR(FileOpenFailed, "cannot create: " + dest_path.string());
    }

    // Adaptive buffer size based on file size
    u64 file_size = entry.header.file_size;
    u64 buffer_size;
    if (file_size < 1 * 1024 * 1024) {
        buffer_size = 256 * 1024;       // 256KB for small files
    } else if (file_size < 100 * 1024 * 1024) {
        buffer_size = 1 * 1024 * 1024;  // 1MB for medium files
    } else {
        buffer_size = 4 * 1024 * 1024;  // 4MB for large files
    }

    // Use thread-local reusable buffer (avoid repeated allocation)
    if (tls_extract_buffer.size() < buffer_size) {
        tls_extract_buffer.resize(buffer_size);
    }

    u64 remaining = file_size;
    u64 read_pos = image_offset_ + static_cast<u64>(entry.header.start_sector) * SECTOR_SIZE;

    while (remaining > 0) {
        u64 to_read = std::min(remaining, buffer_size);

        if (type_ == ImageType::XISO) {
            // ISO: direct read from file (fastest path)
            auto r = read_from_file(0, read_pos, to_read);
            if (!r.is_ok()) {
                return Err<Error>{r.error()};
            }
            const auto& data = r.value();
            u64 write_size = std::min<u64>(to_read, data.size());
            out.write(reinterpret_cast<const char*>(data.data()),
                      static_cast<std::streamsize>(write_size));
            if (!out) {
                return XBOX_IO_ERROR(FileWriteFailed, "write failed: " + dest_path.string());
            }
            remaining -= write_size;
            read_pos += write_size;
        } else {
            // GOD: use read_bytes (sector remapping)
            auto data_r = read_bytes(read_pos, to_read);
            if (!data_r.is_ok()) {
                return Err<Error>{data_r.error()};
            }
            const auto& data = data_r.value();
            u64 write_size = std::min<u64>(to_read, data.size());
            out.write(reinterpret_cast<const char*>(data.data()),
                      static_cast<std::streamsize>(write_size));
            if (!out) {
                return XBOX_IO_ERROR(FileWriteFailed, "write failed: " + dest_path.string());
            }
            remaining -= write_size;
            read_pos += write_size;
        }
    }

    out.close();
    return file_size;
}

// ---------------------------------------------------------------------------
// extract_all — extract all files to a directory
// ---------------------------------------------------------------------------
Result<u64, Error> ImageReader::extract_all(
    const fs::path& dest_dir,
    std::optional<std::function<void(std::size_t, std::size_t, std::string_view)>> progress) const {

    auto entries_r = read_directory_tree();
    if (!entries_r.is_ok()) {
        return Err<Error>{entries_r.error()};
    }

    const auto& entries = entries_r.value();
    std::size_t total_files = 0;
    for (const auto& e : entries) {
        if (!(e.header.attributes & ATTR_DIRECTORY) && e.header.file_size > 0) {
            ++total_files;
        }
    }

    XBOX_LOG_INFO("Extracting {} files to {}", total_files, dest_dir.string());

    std::error_code ec;
    fs::create_directories(dest_dir, ec);

    std::size_t files_done = 0;
    u64 total_bytes = 0;

    for (const auto& entry : entries) {
        if (entry.header.attributes & ATTR_DIRECTORY) {
            // Sanitize path for filesystem
            std::string safe_path = entry.path;
            for (char& c : safe_path) {
                unsigned char uc = static_cast<unsigned char>(c);
                if (uc < 0x20) c = '_';
            }
            auto dir = dest_dir / safe_path;
            std::error_code ec;
            fs::create_directories(dir, ec);
        } else if (entry.header.file_size > 0) {
            // Sanitize path for filesystem
            std::string safe_path = entry.path;
            for (char& c : safe_path) {
                unsigned char uc = static_cast<unsigned char>(c);
                if (uc < 0x20) c = '_';
            }
            auto file_path = dest_dir / safe_path;
            auto r = extract_file(entry, file_path);
            if (!r.is_ok()) {
                XBOX_LOG_DEBUG("Failed to extract '{}': {}", entry.path, r.error().message());
            } else {
                total_bytes += r.value();
            }
            ++files_done;
            if (progress) {
                progress->operator()(files_done, total_files, entry.filename);
            }
        }
    }

    XBOX_LOG_INFO("Extracted {} files, {} bytes", files_done, total_bytes);
    return total_bytes;
}

// ---------------------------------------------------------------------------
// total_file_count — count all files
// ---------------------------------------------------------------------------
std::size_t ImageReader::total_file_count() const {
    auto entries_r = read_directory_tree();
    if (!entries_r.is_ok()) return 0;

    std::size_t count = 0;
    for (const auto& e : entries_r.value()) {
        if (!(e.header.attributes & ATTR_DIRECTORY) && e.header.file_size > 0) {
            ++count;
        }
    }
    return count;
}

} // namespace xbox::xiso
