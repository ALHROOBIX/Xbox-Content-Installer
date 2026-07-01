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
#include "xbox/io/memory_map.hpp"
#include "xbox/stfs/stfs_file.hpp"
#include "xbox/stfs/stfs_header.hpp"
#include "xbox/stfs/stfs_hash.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace xbox::stfs {

namespace fs = std::filesystem;

class StfsReader {
public:
    StfsReader() = default;
    ~StfsReader() = default;

    StfsReader(StfsReader&&) noexcept = default;
    StfsReader& operator=(StfsReader&&) noexcept = default;
    StfsReader(const StfsReader&) = delete;
    StfsReader& operator=(const StfsReader&) = delete;

    // Open an STFS file by path. Memory-maps the entire file.
    // Set force_read = true to skip mmap and use buffer reading (for NTFS/FUSE).
    [[nodiscard]] static Result<StfsReader, Error> open(const fs::path& p,
                                                         bool force_read = false);

    // Open from an already-mapped buffer (for testing).
    [[nodiscard]] static Result<StfsReader, Error> open_from_memory(
        std::span<const byte> data);

    // ----- Accessors -----
    [[nodiscard]] const StfsHeader& header() const noexcept { return header_; }
    [[nodiscard]] const std::vector<FileEntry>& file_entries() const noexcept { return entries_; }
    [[nodiscard]] u64 file_size() const noexcept { return mmap_.is_open() ? mmap_.size() : buffer_size_; }

    // ----- Block-level access -----

    // Get the file offset of a data block.
    [[nodiscard]] u64 block_offset(u32 block_num) const noexcept {
        return block_to_offset(block_num, header_.header_size,
                               header_.blocks_per_hash_table());
    }

    // Get a span covering a single data block. Bounds-checked.
    [[nodiscard]] Result<std::span<const byte>, Error> block_data(u32 block_num) const;

    // Get a span covering N consecutive data blocks starting at `start_block`.
    // Note: this is NOT a contiguous region in the file - hash tables are
    // interspersed. Use read_blocks_into() for contiguous output.
    [[nodiscard]] Result<std::span<const byte>, Error> block_data_raw(u32 block_num) const;

    // Read a contiguous sequence of blocks into a caller-provided buffer.
    // Skips hash tables automatically. Each block contributes 0x1000 bytes.
    [[nodiscard]] Result<void, Error> read_blocks(
        u32 start_block, u32 block_count, std::span<byte> out) const;

    // Read a single block into out (out must be at least 0x1000 bytes).
    [[nodiscard]] Result<void, Error> read_block(u32 block_num, std::span<byte> out) const;

    // ----- Hash verification -----

    // Verify the SHA1 hash of a single block.
    [[nodiscard]] Result<bool, Error> verify_block(u32 block_num) const;

    // Verify all blocks in the package (call once before extraction).
    // Returns the number of failing blocks (0 = all OK).
    struct VerifyReport {
        std::size_t total_blocks{0};
        std::size_t verified_ok{0};
        std::size_t failed{0};
        std::vector<u32> failed_block_indices{};
    };
    [[nodiscard]] Result<VerifyReport, Error> verify_all_blocks(
        std::optional<std::function<void(std::size_t, std::size_t)>> progress = std::nullopt) const;

    // Verify ONLY blocks in file chains (matches Xenia behavior).
    // Walks each file's block chain (start_block → next_block → 0xFFFFFF)
    // and verifies only those blocks. Free/unused blocks are skipped.
    [[nodiscard]] Result<VerifyReport, Error> verify_file_chain_blocks(
        std::optional<std::function<void(std::size_t, std::size_t)>> progress = std::nullopt) const;

    // ----- File-level access -----

    // Read an entire file (given its FileEntry) into a vector.
    // If `verify` is true, every block is SHA1-verified before being
    // concatenated. On hash failure, returns HashVerificationFailed.
    [[nodiscard]] Result<std::vector<u8>, Error> read_file(
        const FileEntry& entry, bool verify = true) const;

    // Read a file into a caller-provided output stream / writer.
    // Used by the extractor to write directly to the destination file.
    template <typename Writer>
    [[nodiscard]] Result<u64, Error> read_file_to(
        const FileEntry& entry, Writer& writer, bool verify = true) const;

    // ----- Lookup helpers -----

    // Find a file entry by absolute path (e.g. "Data/file.bin").
    // Returns nullptr if not found.
    [[nodiscard]] const FileEntry* find_by_path(std::string_view path) const;

    // Get all entries that are direct children of the given parent
    // (root if path_indicator == 0xFFFF).
    [[nodiscard]] std::vector<const FileEntry*> children_of(
        const FileEntry* parent) const;

    // Resolve the absolute path of an entry (e.g. "Data/file.bin").
    [[nodiscard]] std::string entry_path(const FileEntry& entry) const;

private:
    // Initialize the reader from a buffer (either mmap or in-memory).
    [[nodiscard]] Result<void, Error> initialize(std::span<const byte> data);

    // Internal: get the hash entry for a given block number.
    [[nodiscard]] Result<HashEntry, Error> get_hash_entry(u32 block_num) const;

    // Internal: cached hash tables.
    // We cache one hash table per group (170 blocks). The cache key is
    // block_num / 170.
    mutable std::unordered_map<u32, std::array<HashEntry, 0xAA>> hash_cache_{};
    // Stored as unique_ptr so the StfsReader itself remains movable
    // (std::mutex is non-movable/non-copyable).
    mutable std::unique_ptr<std::mutex> hash_cache_mutex_{std::make_unique<std::mutex>()};

    // The memory map (when opened from disk)
    io::MemoryMap mmap_{};

    // Or: pointer to an external buffer (when opened from memory)
    std::span<const byte> external_buffer_{};
    u64 buffer_size_{0};

    // Parsed structures
    StfsHeader header_{};
    std::vector<FileEntry> entries_{};
};

} // namespace xbox::stfs
