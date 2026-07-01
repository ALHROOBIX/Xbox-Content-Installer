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

#include "xbox/core/errors.hpp"
#include "xbox/core/result.hpp"
#include "xbox/core/types.hpp"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace xbox::io {

namespace fs = std::filesystem;

class MemoryMap {
public:
    MemoryMap() = default;
    ~MemoryMap();

    MemoryMap(const MemoryMap&) = delete;
    MemoryMap& operator=(const MemoryMap&) = delete;
    MemoryMap(MemoryMap&& other) noexcept;
    MemoryMap& operator=(MemoryMap&& other) noexcept;

    // Open a file and read it into memory.
    // Uses mmap first; falls back to read() buffer if mmap fails.
    // Set `force_read` = true to skip mmap entirely (for problematic filesystems).
    [[nodiscard]] static Result<MemoryMap, Error> open(const fs::path& p,
                                                        bool force_read = false);

    // Pointer to the start of the data.
    [[nodiscard]] const u8* data() const noexcept { return data_; }
    [[nodiscard]] std::span<const byte> as_span() const noexcept {
        return {reinterpret_cast<const byte*>(data_), static_cast<std::size_t>(size_)};
    }

    // Mapped size in bytes.
    [[nodiscard]] u64 size() const noexcept { return size_; }

    // Whether data is loaded.
    [[nodiscard]] bool is_open() const noexcept { return data_ != nullptr; }

    // True if using mmap (false if using fallback buffer)
    [[nodiscard]] bool is_mapped() const noexcept { return is_mapped_; }

    // Close explicitly.
    void close() noexcept;

    // Sub-span [start, start+length) with bounds checking.
    [[nodiscard]] Result<std::span<const byte>, Error> subspan(u64 start, u64 length) const;

    // Read a typed value at the given offset (bounds-checked).
    template <typename T>
    [[nodiscard]] Result<T, Error> read_at(u64 offset) const {
        if (offset + sizeof(T) > size_) {
            return XBOX_IO_ERROR(FileReadFailed,
                "read_at<" + std::to_string(sizeof(T)) + ">(" +
                std::to_string(offset) + "): out of bounds (size=" +
                std::to_string(size_) + ")");
        }
        T value;
        std::memcpy(&value, data_ + offset, sizeof(T));
        return value;
    }

    // Read N bytes from offset into out (bounds-checked).
    [[nodiscard]] Result<void, Error> read_bytes(u64 offset, void* out, std::size_t count) const;

private:
    u8* data_{nullptr};
    u64 size_{0};
    bool is_mapped_{false};  // true = mmap, false = buffer

    // Fallback buffer (used when mmap fails)
    std::vector<u8> buffer_;

#if defined(_WIN32)
    void* file_handle_{nullptr};
    void* mapping_handle_{nullptr};
#else
    int  fd_{-1};
#endif

    // Internal: try mmap, return true on success
    [[nodiscard]] Result<void, Error> try_mmap(const fs::path& p);
    // Internal: read entire file into buffer_
    [[nodiscard]] Result<void, Error> read_into_buffer(const fs::path& p);
};

} // namespace xbox::io
