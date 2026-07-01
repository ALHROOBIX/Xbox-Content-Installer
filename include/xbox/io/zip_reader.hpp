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

#ifdef HAS_MINIZIP

#include "xbox/core/result.hpp"
#include "xbox/core/types.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace xbox::io {

namespace fs = std::filesystem;

struct ZipEntry {
    std::string name;        // path within the ZIP
    u64 uncompressed_size;   // size in bytes
    bool is_directory{false};
};

class ZipReader {
public:
    ZipReader();
    ~ZipReader();

    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;
    ZipReader(ZipReader&&) noexcept;
    ZipReader& operator=(ZipReader&&) noexcept;

    // Open a ZIP file
    [[nodiscard]] static Result<ZipReader, Error> open(const fs::path& path);

    // List all entries in the ZIP
    [[nodiscard]] std::vector<ZipEntry> list_entries() const;

    // Extract a single file to a destination path
    [[nodiscard]] Result<void, Error> extract_file(const std::string& entry_name,
                                                     const fs::path& dest_path);

    // Extract all files to a destination directory
    [[nodiscard]] Result<void, Error> extract_all(const fs::path& dest_dir);

    // Read a file's contents into memory
    [[nodiscard]] Result<std::vector<u8>, Error> read_file(const std::string& entry_name);

    [[nodiscard]] bool is_open() const noexcept { return handle_ != nullptr; }
    void close() noexcept;

private:
    void* handle_{nullptr};  // unzFile (opaque)
};

} // namespace xbox::io

#else  // !HAS_MINIZIP

// Stub implementation when minizip is not available
#include "xbox/core/result.hpp"
#include "xbox/core/types.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace xbox::io {

namespace fs = std::filesystem;

struct ZipEntry {
    std::string name;
    u64 uncompressed_size{0};
    bool is_directory{false};
};

class ZipReader {
public:
    [[nodiscard]] static Result<ZipReader, Error> open(const fs::path&) {
        return XBOX_IO_ERROR(FileOpenFailed, "ZIP support not compiled (minizip not found)");
    }
    [[nodiscard]] std::vector<ZipEntry> list_entries() const { return {}; }
    [[nodiscard]] Result<void, Error> extract_all(const fs::path&) {
        return XBOX_IO_ERROR(FileOpenFailed, "ZIP support not compiled");
    }
    [[nodiscard]] Result<std::vector<u8>, Error> read_file(const std::string&) {
        return XBOX_IO_ERROR(FileOpenFailed, "ZIP support not compiled");
    }
    [[nodiscard]] bool is_open() const noexcept { return false; }
    void close() noexcept {}
};

} // namespace xbox::io

#endif // HAS_MINIZIP
