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
#include "xbox/stfs/stfs_reader.hpp"

#include "xbox/core/endian.hpp"
#include "xbox/core/errors.hpp"
#include "xbox/core/logger.hpp"
#include "xbox/utils/string_utils.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <numeric>

namespace xbox::stfs {

namespace {
// Get the raw buffer (either from mmap or external buffer)
std::span<const byte> get_buffer(const io::MemoryMap& mmap,
                                 std::span<const byte> external) {
    if (mmap.is_open()) return mmap.as_span();
    return external;
}
} // namespace

Result<StfsReader, Error> StfsReader::open(const fs::path& p, bool force_read) {
    io::MemoryMap mm;
    XBOX_TRY_ASSIGN(mm, io::MemoryMap::open(p, force_read));
    StfsReader reader;
    reader.mmap_ = std::move(mm);
    XBOX_TRY(reader.initialize(reader.mmap_.as_span()));
    return reader;
}

Result<StfsReader, Error> StfsReader::open_from_memory(std::span<const byte> data) {
    StfsReader reader;
    reader.external_buffer_ = data;
    reader.buffer_size_ = data.size();
    XBOX_TRY(reader.initialize(data));
    return reader;
}

Result<void, Error> StfsReader::initialize(std::span<const byte> data) {
    if (data.size() < stfs::HEADER_SIZE) {
        return XBOX_STFS_ERROR(InvalidHeaderSize,
            "STFS file too small: " + std::to_string(data.size()) +
            " bytes (need >= " + std::to_string(stfs::HEADER_SIZE) + ")");
    }

    // Parse the header
    StfsHeader h;
    XBOX_TRY_ASSIGN(h, parse_header(data));
    header_ = std::move(h);

    // Parse the file listing table
    u32 file_table_block = header_.volume.file_table_block_number;
    u32 file_table_block_count = static_cast<u32>(header_.volume.file_table_block_count);

    if (file_table_block_count == 0) {
        XBOX_LOG_WARN("STFS file table block count is 0 - empty package?");
        return {};
    }

    u64 table_offset = block_to_offset(file_table_block, header_.header_size,
                                        header_.blocks_per_hash_table());
    u64 table_size = static_cast<u64>(file_table_block_count) * stfs::BLOCK_SIZE;

    if (table_offset + table_size > data.size()) {
        return XBOX_STFS_ERROR(InvalidVolumeDescriptor,
            "file table extends beyond end of file: offset=" +
            std::to_string(table_offset) + " size=" + std::to_string(table_size) +
            " file_size=" + std::to_string(data.size()));
    }

    auto table_data = std::span<const byte>(data.data() + table_offset,
                                            static_cast<std::size_t>(table_size));

    std::vector<FileEntry> entries;
    XBOX_TRY_ASSIGN(entries, parse_file_table(table_data));
    entries_ = std::move(entries);

    XBOX_LOG_DEBUG("STFS: parsed {} file entries, title_id={:08X}, content_type={:08X}",
                   entries_.size(), header_.title_id, header_.content_type);

    return {};
}

// ----- Block access -----

Result<std::span<const byte>, Error> StfsReader::block_data(u32 block_num) const {
    auto buffer = get_buffer(mmap_, external_buffer_);
    u64 offset = block_to_offset(block_num, header_.header_size,
                                  header_.blocks_per_hash_table());
    if (offset + stfs::BLOCK_SIZE > buffer.size()) {
        return XBOX_STFS_ERROR(BlockOffsetOutOfBounds,
            "block " + std::to_string(block_num) + " at offset " +
            std::to_string(offset) + " beyond file size " +
            std::to_string(buffer.size()));
    }
    return std::span<const byte>(buffer.data() + offset, stfs::BLOCK_SIZE);
}

Result<std::span<const byte>, Error> StfsReader::block_data_raw(u32 block_num) const {
    return block_data(block_num);
}

Result<void, Error> StfsReader::read_block(u32 block_num, std::span<byte> out) const {
    if (out.size() < stfs::BLOCK_SIZE) {
        return XBOX_STFS_ERROR(InvalidBlockIndex,
            "output buffer too small: " + std::to_string(out.size()));
    }
    std::span<const byte> span;
    XBOX_TRY_ASSIGN(span, block_data(block_num));
    std::memcpy(out.data(), span.data(), stfs::BLOCK_SIZE);
    return {};
}

Result<void, Error> StfsReader::read_blocks(
    u32 start_block, u32 block_count, std::span<byte> out) const {
    if (out.size() < block_count * stfs::BLOCK_SIZE) {
        return XBOX_STFS_ERROR(InvalidBlockIndex,
            "output buffer too small for " + std::to_string(block_count) + " blocks");
    }
    for (u32 i = 0; i < block_count; ++i) {
        std::span<const byte> span;
        XBOX_TRY_ASSIGN(span, block_data(start_block + i));
        std::memcpy(out.data() + i * stfs::BLOCK_SIZE, span.data(), stfs::BLOCK_SIZE);
    }
    return {};
}

// ----- Hash verification -----

Result<HashEntry, Error> StfsReader::get_hash_entry(u32 block_num) const {
    std::lock_guard<std::mutex> lock(*hash_cache_mutex_);
    auto buffer = get_buffer(mmap_, external_buffer_);

    u64 ht_offset = level1_hash_table_offset(block_num, header_.header_size,
                                              header_.blocks_per_hash_table());
    u32 group_index = block_num % stfs::HASH_TABLE_BLOCKS_STEP;
    u64 entry_offset = ht_offset + static_cast<u64>(group_index) * stfs::HASH_ENTRY_SIZE;

    if (entry_offset + stfs::HASH_ENTRY_SIZE > buffer.size()) {
        return XBOX_STFS_ERROR(HashTableMissing,
            "hash entry for block " + std::to_string(block_num) +
            " at offset " + std::to_string(entry_offset) + " out of bounds");
    }

    return parse_hash_entry(buffer.data() + entry_offset);
}

Result<bool, Error> StfsReader::verify_block(u32 block_num) const {
    std::span<const byte> block_span;
    XBOX_TRY_ASSIGN(block_span, block_data(block_num));
    HashEntry entry;
    XBOX_TRY_ASSIGN(entry, get_hash_entry(block_num));

    if (entry.is_unused() || entry.is_free()) {
        return true;
    }

    auto computed = crypto::SHA1::compute(block_span.data(), stfs::BLOCK_SIZE);
    return crypto::SHA1::equal(computed, entry.hash);
}

Result<StfsReader::VerifyReport, Error> StfsReader::verify_all_blocks(
    std::optional<std::function<void(std::size_t, std::size_t)>> progress) const {
    VerifyReport report;
    report.total_blocks = static_cast<std::size_t>(
        header_.volume.total_allocated_blocks +
        header_.volume.total_unallocated_blocks);

    auto buffer = get_buffer(mmap_, external_buffer_);
    if (buffer.size() < 0xC000) {
        return report;  // file too small to have any blocks
    }
    std::size_t max_blocks = (buffer.size() - 0xC000) / stfs::BLOCK_SIZE;
    if (report.total_blocks > max_blocks) {
        report.total_blocks = max_blocks;
    }

    for (std::size_t i = 0; i < report.total_blocks; ++i) {
        auto ok = verify_block(static_cast<u32>(i));
        if (!ok.is_ok()) {
            // Block is beyond file size - this is normal for CON files where
            // total_allocated_blocks may include hash table blocks or the
            // last block is incomplete. Skip these silently.
            XBOX_LOG_DEBUG("verify_block({}) skipped: {}", i, ok.error().message());
            continue;
        }
        if (ok.value()) {
            ++report.verified_ok;
        } else {
            ++report.failed;
            report.failed_block_indices.push_back(static_cast<u32>(i));
        }
        if (progress) progress->operator()(i + 1, report.total_blocks);
    }
    return report;
}

// Verify ONLY blocks in file chains — matches Xenia behavior.
// Xenia walks file chains via GetBlockHash().level0_next_block() and
// never verifies free/unused blocks. We do the same.
Result<StfsReader::VerifyReport, Error> StfsReader::verify_file_chain_blocks(
    std::optional<std::function<void(std::size_t, std::size_t)>> progress) const {
    VerifyReport report;
    constexpr u32 kEndOfChain = 0xFFFFFF;

    // Build list of all blocks in all file chains
    std::vector<u32> chain_blocks;
    for (const auto& entry : entries_) {
        if (entry.is_directory()) continue;
        u32 current = entry.starting_block_number;
        u32 blocks_to_read = (entry.file_size + stfs::BLOCK_SIZE - 1) / stfs::BLOCK_SIZE;
        if (blocks_to_read == 0) blocks_to_read = 1;
        for (u32 i = 0; i < blocks_to_read && current != kEndOfChain; ++i) {
            chain_blocks.push_back(current);
            auto next_r = get_hash_entry(current);
            if (!next_r.is_ok()) break;
            current = next_r.value().next_block();
        }
    }
    report.total_blocks = chain_blocks.size();

    // Verify each block
    for (std::size_t i = 0; i < chain_blocks.size(); ++i) {
        auto ok = verify_block(chain_blocks[i]);
        if (!ok.is_ok()) {
            // I/O error — skip (matches Xenia)
        } else if (ok.value()) {
            ++report.verified_ok;
        } else {
            ++report.failed;
            report.failed_block_indices.push_back(chain_blocks[i]);
        }
        if (progress) progress->operator()(i + 1, report.total_blocks);
    }
    return report;
}

// ----- File access -----

Result<std::vector<u8>, Error> StfsReader::read_file(
    const FileEntry& entry, bool verify) const {
    if (entry.is_directory()) {
        return XBOX_STFS_ERROR(InvalidFileEntry,
            "read_file called on directory: " + entry.name);
    }

    std::vector<u8> out;
    out.reserve(entry.file_size);

    u32 remaining = entry.file_size;
    u32 current_block = entry.starting_block_number;

    u32 blocks_to_read = (entry.file_size + stfs::BLOCK_SIZE - 1) / stfs::BLOCK_SIZE;
    if (blocks_to_read == 0) blocks_to_read = 1;

    for (u32 i = 0; i < blocks_to_read; ++i) {
        std::span<const byte> block_span;
        XBOX_TRY_ASSIGN(block_span, block_data(current_block));

        if (verify) {
            auto hash_r = get_hash_entry(current_block);
            if (hash_r.is_ok()) {
                const auto& entry_hash = hash_r.value();
                if (!entry_hash.is_unused() && !entry_hash.is_free()) {
                    auto computed = crypto::SHA1::compute(block_span.data(), stfs::BLOCK_SIZE);
                    if (!crypto::SHA1::equal(computed, entry_hash.hash)) {
                        return XBOX_STFS_ERROR(HashVerificationFailed,
                            "block " + std::to_string(current_block) +
                            " of file '" + entry.name + "' failed SHA1 verification");
                    }
                }
            }
            // If get_hash_entry fails, skip verification for this block
        }

        u32 to_copy = std::min<u32>(remaining, stfs::BLOCK_SIZE);
        const u8* data_ptr = reinterpret_cast<const u8*>(block_span.data());
        out.insert(out.end(), data_ptr, data_ptr + to_copy);
        remaining -= to_copy;

        if (i + 1 < blocks_to_read) {
            // Try to follow the block chain via hash entry's next_block
            auto hash_r = get_hash_entry(current_block);
            if (hash_r.is_ok()) {
                const auto& entry_hash = hash_r.value();
                u32 next = entry_hash.next_block();
                if (next == stfs::END_OF_CHAIN) {
                    // End of chain - but we still have blocks to read
                    // Fall back to contiguous (file may use contiguous allocation)
                    ++current_block;
                } else {
                    // Follow the chain (next=0 is a valid block index)
                    current_block = next;
                }
            } else {
                // Hash entry not available - fall back to contiguous blocks
                ++current_block;
            }
        }
    }

    return out;
}

// ----- Lookup helpers -----

const FileEntry* StfsReader::find_by_path(std::string_view path) const {
    auto parts = str::split(path, '/');
    if (parts.empty()) return nullptr;

    std::vector<std::size_t> current_level;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].path_indicator == -1) {
            current_level.push_back(i);
        }
    }

    for (std::size_t part_idx = 0; part_idx < parts.size(); ++part_idx) {
        const auto& part = parts[part_idx];
        std::size_t found = entries_.size();
        for (auto idx : current_level) {
            if (entries_[idx].name == part) {
                found = idx;
                break;
            }
        }
        if (found == entries_.size()) return nullptr;
        if (part_idx == parts.size() - 1) {
            return &entries_[found];
        }
        current_level.clear();
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].path_indicator == static_cast<i16>(found)) {
                current_level.push_back(i);
            }
        }
    }
    return nullptr;
}

std::vector<const FileEntry*> StfsReader::children_of(const FileEntry* parent) const {
    std::vector<const FileEntry*> out;
    i16 parent_idx = -1;
    if (parent) {
        // Find index of parent in entries_
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (&entries_[i] == parent) {
                parent_idx = static_cast<i16>(i);
                break;
            }
        }
    }

    for (const auto& e : entries_) {
        if (e.path_indicator == parent_idx) {
            out.push_back(&e);
        }
    }
    return out;
}

std::string StfsReader::entry_path(const FileEntry& entry) const {
    // Walk up the parent chain
    std::vector<std::string> parts;
    const FileEntry* current = &entry;
    while (current && current->path_indicator != -1) {
        parts.push_back(current->name);
        std::size_t parent_idx = static_cast<std::size_t>(current->path_indicator);
        if (parent_idx >= entries_.size()) break;
        current = &entries_[parent_idx];
    }
    if (current) parts.push_back(current->name);
    std::reverse(parts.begin(), parts.end());

    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += "/";
        result += parts[i];
    }
    return result;
}

} // namespace xbox::stfs
