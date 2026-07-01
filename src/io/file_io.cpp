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
#include "xbox/io/file_io.hpp"

#include "xbox/core/errors.hpp"

#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

#if defined(_WIN32)
#  include <io.h>
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace xbox::io {

namespace fs = std::filesystem;

File::~File() { close(); }

File::File(File&& other) noexcept
    : path_(std::move(other.path_)), stream_(std::move(other.stream_)) {
    other.path_.clear();
}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        close();
        path_ = std::move(other.path_);
        stream_ = std::move(other.stream_);
        other.path_.clear();
    }
    return *this;
}

namespace {
std::ios::openmode to_openmode(OpenMode m) {
    switch (m) {
        case OpenMode::Read:      return std::ios::in;
        case OpenMode::Write:     return std::ios::out | std::ios::trunc;
        case OpenMode::ReadWrite: return std::ios::in | std::ios::out;
        case OpenMode::Append:    return std::ios::out | std::ios::app;
        case OpenMode::Truncate:  return std::ios::out | std::ios::trunc;
    }
    return std::ios::in;
}
} // namespace

Result<File, Error> File::open(const fs::path& p, OpenMode mode) {
    File f;
    f.path_ = p;
    f.stream_.open(p, to_openmode(mode) | std::ios::binary);
    if (!f.stream_) {
        return XBOX_IO_ERROR(FileOpenFailed,
            "Failed to open file: " + p.string());
    }
    return f;
}

void File::close() noexcept {
    if (stream_.is_open()) {
        stream_.close();
    }
    path_.clear();
}

Result<u64, Error> File::position() {
    if (!is_open()) return XBOX_IO_ERROR(FileStatFailed, "File not open");
    auto pos = stream_.tellg();
    if (pos < 0) {
        return XBOX_IO_ERROR(FileSeekFailed, "tellg failed on " + path_.string());
    }
    return static_cast<u64>(pos);
}

Result<u64, Error> File::size() const {
    if (!is_open()) return XBOX_IO_ERROR(FileStatFailed, "File not open");
    std::error_code ec;
    auto s = fs::file_size(path_, ec);
    if (ec) {
        return XBOX_IO_ERROR(FileStatFailed, "file_size(" + path_.string() + "): " + ec.message());
    }
    return s;
}

Result<void, Error> File::seek(u64 pos) {
    if (!is_open()) return XBOX_IO_ERROR(FileSeekFailed, "File not open");
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(pos), std::ios::beg);
    stream_.seekp(static_cast<std::streamoff>(pos), std::ios::beg);
    if (!stream_) {
        return XBOX_IO_ERROR(FileSeekFailed,
            "seek(" + std::to_string(pos) + ") on " + path_.string());
    }
    return {};
}

Result<void, Error> File::seek_relative(i64 offset) {
    if (!is_open()) return XBOX_IO_ERROR(FileSeekFailed, "File not open");
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(offset), std::ios::cur);
    if (!stream_) {
        return XBOX_IO_ERROR(FileSeekFailed,
            "seek_relative(" + std::to_string(offset) + ") on " + path_.string());
    }
    return {};
}

Result<std::size_t, Error> File::read(void* out, std::size_t count) {
    if (!is_open()) return XBOX_IO_ERROR(FileReadFailed, "File not open");
    if (count == 0) return std::size_t{0};

    // Cap to avoid std::numeric_limits<std::streamsize>::max() overflow
    constexpr std::size_t MAX_CHUNK =
        static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max());

    std::size_t total = 0;
    while (total < count) {
        std::size_t want = std::min(count - total, MAX_CHUNK);
        stream_.read(static_cast<char*>(out) + total,
                     static_cast<std::streamsize>(want));
        std::size_t got = static_cast<std::size_t>(stream_.gcount());
        total += got;
        if (got < want) break;  // EOF or error
    }
    if (total < count && stream_.bad()) {
        return XBOX_IO_ERROR(FileReadFailed,
            "read error on " + path_.string() + " after " + std::to_string(total) + " bytes");
    }
    return total;
}

Result<std::size_t, Error> File::read(std::span<byte> out) {
    return read(out.data(), out.size());
}

Result<std::vector<u8>, Error> File::read_all() {
    u64 sz;
    XBOX_TRY_ASSIGN(sz, size());
    std::vector<u8> buf;
    buf.resize(sz);
    XBOX_TRY(seek(0));
    std::size_t got;
    XBOX_TRY_ASSIGN(got, read(buf.data(), sz));
    buf.resize(got);
    return buf;
}

Result<std::size_t, Error> File::write(const void* data, std::size_t count) {
    if (!is_open()) return XBOX_IO_ERROR(FileWriteFailed, "File not open");
    if (count == 0) return std::size_t{0};

    constexpr std::size_t MAX_CHUNK =
        static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max());

    std::size_t total = 0;
    while (total < count) {
        std::size_t want = std::min(count - total, MAX_CHUNK);
        stream_.write(static_cast<const char*>(data) + total,
                      static_cast<std::streamsize>(want));
        if (!stream_) {
            return XBOX_IO_ERROR(FileWriteFailed,
                "write error on " + path_.string() + " after " + std::to_string(total) + " bytes");
        }
        total += want;
    }
    return total;
}

Result<std::size_t, Error> File::write(std::span<const byte> data) {
    return write(data.data(), data.size());
}

void File::flush() {
    if (is_open()) stream_.flush();
}

Result<void, Error> File::sync() {
    if (!is_open()) return XBOX_IO_ERROR(FileWriteFailed, "File not open");
    stream_.flush();
#if defined(_WIN32)
    // FlushFileBuffers needs a native handle
    // fstream doesn't expose it directly; rely on destructor flush.
    // For production, we'd use a native HANDLE-based file class.
#else
    (void)0;  // fsync would need fileno() - left as TODO for v1.1
#endif
    return {};
}

// ----- Free functions -----
Result<std::vector<u8>, Error> read_file(const fs::path& p) {
    File f;
    XBOX_TRY_ASSIGN(f, File::open(p, OpenMode::Read));
    return f.read_all();
}

Result<void, Error> write_file(const fs::path& p, std::span<const byte> data) {
    File f;
    XBOX_TRY_ASSIGN(f, File::open(p, OpenMode::Truncate));
    std::size_t got;
    XBOX_TRY_ASSIGN(got, f.write(data));
    if (got != data.size()) {
        return XBOX_IO_ERROR(FileWriteFailed,
            "short write to " + p.string());
    }
    return {};
}

Result<void, Error> append_file(const fs::path& p, std::span<const byte> data) {
    File f;
    XBOX_TRY_ASSIGN(f, File::open(p, OpenMode::Append));
    std::size_t got;
    XBOX_TRY_ASSIGN(got, f.write(data));
    if (got != data.size()) {
        return XBOX_IO_ERROR(FileWriteFailed, "short append to " + p.string());
    }
    return {};
}

bool file_exists(const fs::path& p) noexcept {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

bool dir_exists(const fs::path& p) noexcept {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

} // namespace xbox::io
