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
#include "xbox/io/memory_map.hpp"

#include "xbox/core/errors.hpp"
#include "xbox/core/logger.hpp"

#include <cstring>
#include <system_error>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  include <sys/statfs.h>  // for statfs (filesystem type detection)
#endif

namespace xbox::io {

namespace {

#if !defined(_WIN32)
// Filesystem magic numbers for detecting problematic FUSE/NTFS drivers
// that don't properly support mmap (causing SIGSEGV on page access).
// NTFS-3G magic: 0x65735546 ("FUSE" in little-endian)
constexpr int32_t FUSE_SUPER_MAGIC = 0x65735546;
// NTFS magic (kernel driver, less common): 0x5346544e
constexpr int32_t NTFS_SB_MAGIC = 0x5346544e;
// SSHFS also uses FUSE magic
// overlayfs/vboxsf may also have issues on some kernels

// Check if the filesystem at `path` is known to have mmap issues.
// Returns true if we should skip mmap and use buffer reading instead.
bool filesystem_unsafe_for_mmap(const fs::path& p) noexcept {
    struct statfs sfs{};
    if (statfs(p.c_str(), &sfs) != 0) {
        // Can't determine - allow mmap (will fall back on access failure)
        return false;
    }
    // FUSE-based filesystems (ntfs-3g, sshfs, etc.) often have mmap issues
    if (sfs.f_type == static_cast<uint32_t>(FUSE_SUPER_MAGIC)) {
        return true;
    }
    return false;
}
#endif

// Thread-safe strerror replacement
std::string safe_strerror(int err) noexcept {
#if defined(_WIN32)
    char buf[256];
    if (strerror_s(buf, sizeof(buf), err) == 0) {
        return std::string(buf);
    }
    return "error " + std::to_string(err);
#else
    char buf[256];
    // strerror_r is thread-safe; XSI version returns int, GNU version returns char*
    auto result = strerror_r(err, buf, sizeof(buf));
    (void)result;  // suppress unused warning on GNU version
    return std::string(buf);
#endif
}

} // namespace

namespace fs = std::filesystem;

MemoryMap::~MemoryMap() { close(); }

MemoryMap::MemoryMap(MemoryMap&& other) noexcept
    : data_(other.data_), size_(other.size_),
      is_mapped_(other.is_mapped_),
      buffer_(std::move(other.buffer_))
#if defined(_WIN32)
    , file_handle_(other.file_handle_), mapping_handle_(other.mapping_handle_)
#else
    , fd_(other.fd_)
#endif
{
#if defined(_WIN32)
    other.file_handle_ = nullptr;
    other.mapping_handle_ = nullptr;
#else
    other.fd_ = -1;
#endif
    other.data_ = nullptr;
    other.size_ = 0;
    other.is_mapped_ = false;
}

MemoryMap& MemoryMap::operator=(MemoryMap&& other) noexcept {
    if (this != &other) {
        close();
        data_ = other.data_;
        size_ = other.size_;
        is_mapped_ = other.is_mapped_;
        buffer_ = std::move(other.buffer_);
#if defined(_WIN32)
        file_handle_ = other.file_handle_;
        mapping_handle_ = other.mapping_handle_;
        other.file_handle_ = nullptr;
        other.mapping_handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
        other.data_ = nullptr;
        other.size_ = 0;
        other.is_mapped_ = false;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// try_mmap: attempt to memory-map the file
// Returns success only if mmap succeeds AND the first byte is readable
// ---------------------------------------------------------------------------
Result<void, Error> MemoryMap::try_mmap(const fs::path& p) {
#if defined(_WIN32)
    // Use full FILE_SHARE to allow other processes (like Xenia) to access the file
    HANDLE h = CreateFileW(p.wstring().c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return XBOX_IO_ERROR(FileOpenFailed,
            "CreateFile failed: " + std::to_string(GetLastError()));
    }

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz)) {
        CloseHandle(h);
        return XBOX_IO_ERROR(FileStatFailed,
            "GetFileSizeEx failed: " + std::to_string(GetLastError()));
    }
    if (sz.QuadPart == 0) {
        CloseHandle(h);
        return XBOX_IO_ERROR(FileTooSmall, "file is empty");
    }

    HANDLE mapping = CreateFileMappingW(h, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping) {
        CloseHandle(h);
        return XBOX_IO_ERROR(FileOpenFailed,
            "CreateFileMapping failed: " + std::to_string(GetLastError()));
    }

    LPVOID view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        CloseHandle(mapping);
        CloseHandle(h);
        return XBOX_IO_ERROR(FileOpenFailed,
            "MapViewOfFile failed: " + std::to_string(GetLastError()));
    }

    data_ = static_cast<u8*>(view);
    size_ = static_cast<u64>(sz.QuadPart);
    file_handle_ = h;
    mapping_handle_ = mapping;
    is_mapped_ = true;

    return {};
#else
    // Check if filesystem is known to be unsafe for mmap (ntfs-3g, FUSE)
    if (filesystem_unsafe_for_mmap(p)) {
        XBOX_LOG_DEBUG("MemoryMap: filesystem at {} is FUSE-based, skipping mmap",
                       p.string());
        return XBOX_IO_ERROR(FileOpenFailed,
            "filesystem unsafe for mmap (FUSE/NTFS-3G detected)");
    }

    int fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return XBOX_IO_ERROR(FileOpenFailed,
            "open() failed: " + safe_strerror(errno));
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        int e = errno;
        ::close(fd);
        return XBOX_IO_ERROR(FileStatFailed,
            "fstat: " + safe_strerror(e));
    }
    if (st.st_size == 0) {
        ::close(fd);
        return XBOX_IO_ERROR(FileTooSmall, "file is empty");
    }

    void* addr = mmap(nullptr, static_cast<std::size_t>(st.st_size),
                      PROT_READ, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) {
        int e = errno;
        ::close(fd);
        return XBOX_IO_ERROR(FileOpenFailed,
            "mmap: " + safe_strerror(e));
    }

    // Use pread to verify the file is readable WITHOUT accessing mmap pages
    // (avoids SIGSEGV on broken FUSE drivers that we didn't catch above)
    u8 probe[1];
    ssize_t n = ::pread(fd, probe, 1, 0);
    if (n != 1) {
        int e = errno;
        munmap(addr, static_cast<std::size_t>(st.st_size));
        ::close(fd);
        return XBOX_IO_ERROR(FileOpenFailed,
            "pread probe failed (filesystem may not support mmap): " + safe_strerror(e));
    }

    data_ = static_cast<u8*>(addr);
    size_ = static_cast<u64>(st.st_size);
    fd_ = fd;
    is_mapped_ = true;

    // Advise the kernel we'll read sequentially
    (void)posix_madvise(addr, static_cast<std::size_t>(st.st_size), MADV_SEQUENTIAL);

    return {};
#endif
}

// ---------------------------------------------------------------------------
// read_into_buffer: fallback - read entire file into a std::vector
// ---------------------------------------------------------------------------
Result<void, Error> MemoryMap::read_into_buffer(const fs::path& p) {
    std::error_code ec;
    auto file_size = fs::file_size(p, ec);
    if (ec) {
        return XBOX_IO_ERROR(FileStatFailed,
            "file_size: " + ec.message());
    }
    if (file_size == 0) {
        return XBOX_IO_ERROR(FileTooSmall, "file is empty");
    }

    buffer_.resize(file_size);

#if defined(_WIN32)
    HANDLE h = CreateFileW(p.wstring().c_str(),
        GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return XBOX_IO_ERROR(FileOpenFailed,
            "CreateFile failed: " + std::to_string(GetLastError()));
    }

    DWORD total_read = 0;
    while (total_read < file_size) {
        DWORD to_read = static_cast<DWORD>(std::min<u64>(file_size - total_read, 0x40000000));
        DWORD bytes_read = 0;
        if (!ReadFile(h, buffer_.data() + total_read, to_read, &bytes_read, nullptr) || bytes_read == 0) {
            CloseHandle(h);
            return XBOX_IO_ERROR(FileReadFailed,
                "ReadFile failed: " + std::to_string(GetLastError()));
        }
        total_read += bytes_read;
    }
    CloseHandle(h);
#else
    int fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return XBOX_IO_ERROR(FileOpenFailed,
            "open() failed: " + safe_strerror(errno));
    }

    std::size_t total_read = 0;
    while (total_read < buffer_.size()) {
        ssize_t n = ::read(fd, buffer_.data() + total_read,
                           buffer_.size() - total_read);
        if (n < 0) {
            if (errno == EINTR) continue;
            int e = errno;
            ::close(fd);
            return XBOX_IO_ERROR(FileReadFailed,
                "read: " + safe_strerror(e));
        }
        if (n == 0) break;  // EOF
        total_read += static_cast<std::size_t>(n);
    }
    ::close(fd);
#endif

    data_ = buffer_.data();
    size_ = file_size;
    is_mapped_ = false;
    return {};
}

// ---------------------------------------------------------------------------
// open: try mmap first, fall back to buffer reading on failure
// ---------------------------------------------------------------------------
Result<MemoryMap, Error> MemoryMap::open(const fs::path& p, bool force_read) {
    MemoryMap mm;

    if (force_read) {
        // Skip mmap entirely (--no-mmap flag)
        XBOX_LOG_DEBUG("MemoryMap: using buffer mode (forced) for {}", p.string());
        auto r = mm.read_into_buffer(p);
        if (!r.is_ok()) {
            return std::move(r).error();
        }
        return mm;
    }

    // Try mmap first
    auto mmap_r = mm.try_mmap(p);
    if (mmap_r.is_ok()) {
        XBOX_LOG_DEBUG("MemoryMap: mmap succeeded for {} ({} bytes)",
                       p.string(), mm.size_);
        return mm;
    }

    // mmap failed - log and fall back to buffer reading
    XBOX_LOG_WARN("MemoryMap: mmap failed for {} ({}), falling back to buffer reading",
                  p.string(), mmap_r.error().message());

    // Reset any partial state
    mm.close();

    auto buf_r = mm.read_into_buffer(p);
    if (!buf_r.is_ok()) {
        return std::move(buf_r).error();
    }

    XBOX_LOG_DEBUG("MemoryMap: buffer read succeeded for {} ({} bytes)",
                   p.string(), mm.size_);
    return mm;
}

void MemoryMap::close() noexcept {
    if (is_mapped_) {
#if defined(_WIN32)
        if (data_) UnmapViewOfFile(data_);
        if (mapping_handle_) CloseHandle(mapping_handle_);
        if (file_handle_) CloseHandle(file_handle_);
        mapping_handle_ = nullptr;
        file_handle_ = nullptr;
#else
        if (data_) munmap(data_, static_cast<std::size_t>(size_));
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
#endif
    }
    // If using buffer, it's automatically freed when buffer_ is destroyed

    data_ = nullptr;
    size_ = 0;
    is_mapped_ = false;
    buffer_.clear();
    buffer_.shrink_to_fit();
}

Result<std::span<const byte>, Error> MemoryMap::subspan(u64 start, u64 length) const {
    if (!data_) {
        return XBOX_IO_ERROR(FileReadFailed, "MemoryMap: not open");
    }
    if (start > size_ || length > size_ - start) {
        return XBOX_IO_ERROR(FileReadFailed,
            "MemoryMap::subspan out of bounds: start=" + std::to_string(start) +
            " len=" + std::to_string(length) + " size=" + std::to_string(size_));
    }
    return std::span<const byte>{
        reinterpret_cast<const byte*>(data_ + start),
        static_cast<std::size_t>(length)};
}

Result<void, Error> MemoryMap::read_bytes(u64 offset, void* out, std::size_t count) const {
    auto span_r = subspan(offset, count);
    if (!span_r.is_ok()) {
        return Err<Error>{span_r.error()};
    }
    std::memcpy(out, span_r.value().data(), count);
    return {};
}

} // namespace xbox::io
