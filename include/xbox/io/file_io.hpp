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

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace xbox::io {

namespace fs = std::filesystem;

enum class OpenMode : u8 {
    Read,
    Write,
    ReadWrite,
    Append,
    Truncate,  // create or truncate
};

class File {
public:
    File() = default;
    ~File();

    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;

    // Open a file with the given mode.
    [[nodiscard]] static Result<File, Error> open(const fs::path& p, OpenMode mode);

    // Check if currently open.
    [[nodiscard]] bool is_open() const noexcept { return stream_.is_open(); }

    // Close explicitly (also called by destructor).
    void close() noexcept;

    // Get the file size. Returns 0 on empty file; error if not open.
    [[nodiscard]] Result<u64, Error> size() const;

    // Get the current read/write position.
    [[nodiscard]] Result<u64, Error> position();

    // Seek to an absolute position.
    [[nodiscard]] Result<void, Error> seek(u64 pos);

    // Seek relative to current position.
    [[nodiscard]] Result<void, Error> seek_relative(i64 offset);

    // Read exactly `count` bytes into `out`. Returns the number of bytes
    // actually read (may be less if EOF reached before count).
    [[nodiscard]] Result<std::size_t, Error> read(void* out, std::size_t count);

    // Read into a span. Returns bytes read.
    [[nodiscard]] Result<std::size_t, Error> read(std::span<byte> out);

    // Read the entire file into a vector of bytes.
    [[nodiscard]] Result<std::vector<u8>, Error> read_all();

    // Write exactly `count` bytes. Returns the number of bytes actually written.
    [[nodiscard]] Result<std::size_t, Error> write(const void* data, std::size_t count);

    // Write from a span. Returns bytes written.
    [[nodiscard]] Result<std::size_t, Error> write(std::span<const byte> data);

    // Flush to OS buffers (does NOT force disk sync unless sync() called).
    void flush();

    // Force OS-level write to disk (fsync / FlushFileBuffers).
    [[nodiscard]] Result<void, Error> sync();

    // The underlying path (empty if not open).
    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
    std::fstream stream_;
};

// Convenience free functions
[[nodiscard]] Result<std::vector<u8>, Error> read_file(const fs::path& p);
[[nodiscard]] Result<void, Error> write_file(const fs::path& p, std::span<const byte> data);
[[nodiscard]] Result<void, Error> append_file(const fs::path& p, std::span<const byte> data);

// Check whether a file exists at the path and is a regular file.
[[nodiscard]] bool file_exists(const fs::path& p) noexcept;

// Check whether a directory exists.
[[nodiscard]] bool dir_exists(const fs::path& p) noexcept;

} // namespace xbox::io
