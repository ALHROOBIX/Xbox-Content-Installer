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
#ifdef HAS_MINIZIP

#include "xbox/io/zip_reader.hpp"

#include "xbox/core/errors.hpp"
#include "xbox/core/logger.hpp"

#include <cstring>
#include <fstream>

// For Linux: use system minizip headers at <minizip/unzip.h>
// For Windows cross-compile: use bundled headers at third_party/minizip/unzip.h
#if defined(_WIN32) || defined(__MINGW32__)
    #include "unzip.h"
#else
    #include <minizip/unzip.h>
#endif

namespace xbox::io {

namespace fs = std::filesystem;

ZipReader::ZipReader() = default;

ZipReader::~ZipReader() {
    close();
}

ZipReader::ZipReader(ZipReader&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

ZipReader& ZipReader::operator=(ZipReader&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

Result<ZipReader, Error> ZipReader::open(const fs::path& path) {
    ZipReader reader;
    reader.handle_ = unzOpen64(path.c_str());
    if (!reader.handle_) {
        return XBOX_IO_ERROR(FileOpenFailed, "Failed to open ZIP: " + path.string());
    }
    return reader;
}

void ZipReader::close() noexcept {
    if (handle_) {
        unzClose(handle_);
        handle_ = nullptr;
    }
}

std::vector<ZipEntry> ZipReader::list_entries() const {
    std::vector<ZipEntry> entries;
    if (!handle_) return entries;

    if (unzGoToFirstFile(handle_) != UNZ_OK) return entries;

    do {
        char filename[1024];
        unz_file_info64 info;
        if (unzGetCurrentFileInfo64(handle_, &info, filename, sizeof(filename),
                                     nullptr, 0, nullptr, 0) == UNZ_OK) {
            ZipEntry entry;
            entry.name = filename;
            entry.uncompressed_size = info.uncompressed_size;
            entry.is_directory = (!entry.name.empty() &&
                                  (entry.name.back() == '/' || entry.name.back() == '\\'));
            entries.push_back(std::move(entry));
        }
    } while (unzGoToNextFile(handle_) == UNZ_OK);

    return entries;
}

Result<std::vector<u8>, Error> ZipReader::read_file(const std::string& entry_name) {
    if (!handle_) {
        return XBOX_IO_ERROR(FileReadFailed, "ZIP not open");
    }

    if (unzLocateFile(handle_, entry_name.c_str(), 0) != UNZ_OK) {
        return XBOX_IO_ERROR(FileNotFound, "Entry not found in ZIP: " + entry_name);
    }

    unz_file_info64 info;
    char filename[1024];
    if (unzGetCurrentFileInfo64(handle_, &info, filename, sizeof(filename),
                                 nullptr, 0, nullptr, 0) != UNZ_OK) {
        return XBOX_IO_ERROR(FileReadFailed, "Failed to get ZIP entry info: " + entry_name);
    }

    if (unzOpenCurrentFile(handle_) != UNZ_OK) {
        return XBOX_IO_ERROR(FileOpenFailed, "Failed to open entry in ZIP: " + entry_name);
    }

    std::vector<u8> data(info.uncompressed_size);
    int total_read = 0;
    while (static_cast<u64>(total_read) < info.uncompressed_size) {
        int n = unzReadCurrentFile(handle_,
                                    data.data() + total_read,
                                    static_cast<unsigned int>(info.uncompressed_size - total_read));
        if (n < 0) {
            unzCloseCurrentFile(handle_);
            return XBOX_IO_ERROR(FileReadFailed, "Error reading ZIP entry: " + entry_name);
        }
        if (n == 0) break;
        total_read += n;
    }

    unzCloseCurrentFile(handle_);
    data.resize(total_read);
    return data;
}

Result<void, Error> ZipReader::extract_file(const std::string& entry_name,
                                               const fs::path& dest_path) {
    auto data_r = read_file(entry_name);
    if (!data_r.is_ok()) {
        return Err<Error>{data_r.error()};
    }
    auto& data = data_r.value();

    // Create parent directories
    auto parent = dest_path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) {
            return XBOX_IO_ERROR(DirectoryCreateFailed,
                "Failed to create directory: " + parent.string() + ": " + ec.message());
        }
    }

    // Write the file
    std::ofstream f(dest_path, std::ios::binary | std::ios::trunc);
    if (!f) {
        return XBOX_IO_ERROR(FileOpenFailed, "Failed to create file: " + dest_path.string());
    }
    if (!data.empty()) {
        f.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
        if (!f) {
            return XBOX_IO_ERROR(FileWriteFailed, "Failed to write file: " + dest_path.string());
        }
    }
    return {};
}

Result<void, Error> ZipReader::extract_all(const fs::path& dest_dir) {
    if (!handle_) {
        return XBOX_IO_ERROR(FileReadFailed, "ZIP not open");
    }

    // Create destination directory
    std::error_code ec;
    fs::create_directories(dest_dir, ec);
    if (ec) {
        return XBOX_IO_ERROR(DirectoryCreateFailed,
            "Failed to create destination directory: " + dest_dir.string());
    }

    if (unzGoToFirstFile(handle_) != UNZ_OK) {
        return XBOX_IO_ERROR(FileReadFailed, "Failed to iterate ZIP entries");
    }

    int extracted = 0;
    do {
        char filename[1024];
        unz_file_info64 info;
        if (unzGetCurrentFileInfo64(handle_, &info, filename, sizeof(filename),
                                     nullptr, 0, nullptr, 0) != UNZ_OK) {
            continue;
        }

        std::string name(filename);
        if (name.empty()) continue;

        // Skip directories (they end with /)
        if (name.back() == '/' || name.back() == '\\') {
            auto dir_path = dest_dir / name;
            fs::create_directories(dir_path, ec);
            continue;
        }

        // Extract the file
        auto dest = dest_dir / name;
        auto r = extract_file(name, dest);
        if (!r.is_ok()) {
            XBOX_LOG_WARN("Failed to extract {}: {}", name, r.error().message());
        } else {
            ++extracted;
        }
    } while (unzGoToNextFile(handle_) == UNZ_OK);

    XBOX_LOG_INFO("Extracted {} files from ZIP to {}", extracted, dest_dir.string());
    return {};
}

} // namespace xbox::io

#endif // HAS_MINIZIP
