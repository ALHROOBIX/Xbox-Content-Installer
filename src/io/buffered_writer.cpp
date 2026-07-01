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
#include "xbox/io/buffered_writer.hpp"

#include "xbox/core/errors.hpp"

#include <cstring>
#include <system_error>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <unistd.h>
#  include <fcntl.h>
#endif

namespace xbox::io {

namespace fs = std::filesystem;

BufferedWriter::BufferedWriter(std::size_t buffer_size)
    : buffer_(buffer_size) {}

BufferedWriter::~BufferedWriter() {
    if (is_open()) {
        // Best-effort flush on destruction
        try { (void)close(); } catch (...) {}
    }
}

BufferedWriter::BufferedWriter(BufferedWriter&& other) noexcept
    : fd_(other.fd_),
      buffer_(std::move(other.buffer_)),
      buffer_pos_(other.buffer_pos_),
      total_bytes_(other.total_bytes_),
      path_(std::move(other.path_))
#if defined(_WIN32)
    , file_handle_(other.file_handle_)
#endif
{
    other.fd_ = invalid_fd;
#if defined(_WIN32)
    other.file_handle_ = nullptr;
#endif
    other.buffer_pos_ = 0;
    other.total_bytes_ = 0;
}

BufferedWriter& BufferedWriter::operator=(BufferedWriter&& other) noexcept {
    if (this != &other) {
        if (is_open()) (void)close();
        fd_ = other.fd_;
        buffer_ = std::move(other.buffer_);
        buffer_pos_ = other.buffer_pos_;
        total_bytes_ = other.total_bytes_;
        path_ = std::move(other.path_);
#if defined(_WIN32)
        file_handle_ = other.file_handle_;
        other.file_handle_ = nullptr;
#endif
        other.fd_ = invalid_fd;
        other.buffer_pos_ = 0;
        other.total_bytes_ = 0;
    }
    return *this;
}

Result<void, Error> BufferedWriter::open(const fs::path& p) {
    if (is_open()) (void)close();
    path_ = p;

#if defined(_WIN32)
    HANDLE h = CreateFileW(p.wstring().c_str(),
        GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        return XBOX_IO_ERROR(FileOpenFailed,
            "BufferedWriter: CreateFile failed: " + std::to_string(err) + " path=" + p.string());
    }
    file_handle_ = h;
    fd_ = 0;  // mark as open
#else
    int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return XBOX_IO_ERROR(FileOpenFailed,
            std::string("BufferedWriter: open: ") + std::strerror(errno));
    }
    fd_ = fd;
#endif

    buffer_pos_ = 0;
    total_bytes_ = 0;
    return {};
}

namespace {
Result<std::size_t, Error> write_all_native(
#if defined(_WIN32)
    HANDLE h,
#else
    int fd,
#endif
    const u8* data, std::size_t count, const fs::path& path)
{
    std::size_t total = 0;
    while (total < count) {
#if defined(_WIN32)
        DWORD want = static_cast<DWORD>(std::min<std::size_t>(count - total, 0x40000000));
        DWORD got = 0;
        if (!WriteFile(h, data + total, want, &got, nullptr)) {
            DWORD err = GetLastError();
            return XBOX_IO_ERROR(FileWriteFailed,
                "WriteFile failed: " + std::to_string(err) + " path=" + path.string());
        }
        if (got == 0) {
            return XBOX_IO_ERROR(FileWriteFailed, "WriteFile returned 0 (disk full?)");
        }
        total += got;
#else
        ssize_t got = ::write(fd, data + total, count - total);
        if (got < 0) {
            if (errno == EINTR) continue;
            return XBOX_IO_ERROR(FileWriteFailed,
                std::string("write: ") + std::strerror(errno) + " path=" + path.string());
        }
        if (got == 0) break;
        total += static_cast<std::size_t>(got);
#endif
    }
    return total;
}
} // namespace

Result<void, Error> BufferedWriter::write(const void* data, std::size_t count) {
    if (!is_open()) {
        return XBOX_IO_ERROR(FileWriteFailed, "BufferedWriter: not open");
    }

    const auto* p = static_cast<const u8*>(data);

    // If data is larger than the buffer, flush + write directly
    if (count >= buffer_.size()) {
        XBOX_TRY(flush());
        std::size_t wrote;
        XBOX_TRY_ASSIGN(wrote, write_all_native(
#if defined(_WIN32)
            file_handle_,
#else
            fd_,
#endif
            p, count, path_));
        total_bytes_ += wrote;
        return {};
    }

    // Otherwise, fill buffer + flush as needed
    if (buffer_pos_ + count > buffer_.size()) {
        XBOX_TRY(flush());
    }
    std::memcpy(buffer_.data() + buffer_pos_, p, count);
    buffer_pos_ += count;
    total_bytes_ += count;
    return {};
}

Result<void, Error> BufferedWriter::write(std::span<const byte> data) {
    return write(data.data(), data.size());
}

Result<void, Error> BufferedWriter::flush() {
    if (!is_open()) return {};
    if (buffer_pos_ == 0) return {};

    std::size_t wrote;
    XBOX_TRY_ASSIGN(wrote, write_all_native(
#if defined(_WIN32)
        file_handle_,
#else
        fd_,
#endif
        buffer_.data(), buffer_pos_, path_));
    if (wrote != buffer_pos_) {
        return XBOX_IO_ERROR(FileWriteFailed, "short write in flush()");
    }
    buffer_pos_ = 0;
    return {};
}

Result<void, Error> BufferedWriter::sync() {
    XBOX_TRY(flush());
#if defined(_WIN32)
    if (!FlushFileBuffers(file_handle_)) {
        DWORD err = GetLastError();
        return XBOX_IO_ERROR(FileWriteFailed, "FlushFileBuffers: " + std::to_string(err));
    }
#else
    if (fsync(fd_) != 0) {
        return XBOX_IO_ERROR(FileWriteFailed, std::string("fsync: ") + std::strerror(errno));
    }
#endif
    return {};
}

Result<void, Error> BufferedWriter::close() {
    if (!is_open()) return {};
    auto fr = flush();
#if defined(_WIN32)
    CloseHandle(file_handle_);
    file_handle_ = nullptr;
#else
    ::close(fd_);
    fd_ = invalid_fd;
#endif
    return fr;
}

} // namespace xbox::io
