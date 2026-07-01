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
#include "xbox/utils/path_utils.hpp"
#include "xbox/core/errors.hpp"
#include "xbox/utils/string_utils.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace xbox::path {

namespace fs = std::filesystem;

std::optional<fs::path> safe_join(const fs::path& parent, std::string_view child) {
    // Reject path-traversal attempts
    if (child.empty()) return parent;
    if (child.starts_with("/") || child.starts_with("\\")) return std::nullopt;
    if (child == ".." || child.starts_with("../") || child.starts_with("..\\")) return std::nullopt;
    if (child.find("/..") != std::string_view::npos) return std::nullopt;
    if (child.find("\\..") != std::string_view::npos) return std::nullopt;

    fs::path child_path(child);
    auto result = parent / child_path;

    // Verify the result is still under parent (handle symlinks etc by canonicalizing)
    std::error_code ec;
    auto parent_canon = fs::weakly_canonical(parent, ec);
    if (ec) parent_canon = parent;
    auto result_canon = fs::weakly_canonical(result, ec);
    if (ec) result_canon = result;

    // Check that result_canon starts with parent_canon
    auto mismatch = std::mismatch(parent_canon.begin(), parent_canon.end(),
                                  result_canon.begin(), result_canon.end());
    if (mismatch.first != parent_canon.end()) {
        return std::nullopt;
    }
    return result;
}

std::optional<fs::path> safe_join_many(const fs::path& parent,
    const std::vector<std::string>& children) {
    fs::path cur = parent;
    for (const auto& c : children) {
        auto next = safe_join(cur, c);
        if (!next) return std::nullopt;
        cur = *next;
    }
    return cur;
}

Result<fs::path, Error> ensure_directory(const fs::path& p) {
    std::error_code ec;
    if (fs::exists(p, ec)) {
        if (!fs::is_directory(p, ec)) {
            return XBOX_IO_ERROR(DirectoryCreateFailed,
                p.string() + " exists but is not a directory");
        }
        return p;
    }
    if (fs::create_directories(p, ec); ec) {
        return XBOX_IO_ERROR(DirectoryCreateFailed,
            "Failed to create directory " + p.string() + ": " + ec.message());
    }
    return p;
}

Result<void, Error> atomic_replace(const fs::path& from, const fs::path& to) {
#if defined(_WIN32)
    // Windows: use MoveFileEx with MOVEFILE_REPLACE_EXISTING for atomic replace
    if (MoveFileExW(from.wstring().c_str(), to.wstring().c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return {};
    }
    DWORD err = GetLastError();
    return XBOX_IO_ERROR(FileWriteFailed,
        "MoveFileEx failed: " + std::to_string(err) +
        " (" + from.string() + " -> " + to.string() + ")");
#else
    // POSIX: rename() is atomic and replaces target by default
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) return {};

    return XBOX_IO_ERROR(FileWriteFailed,
        "atomic_replace " + from.string() + " -> " + to.string() + ": " + ec.message());
#endif
}

namespace {
// Generate a unique temp filename in the same directory as `target`.
fs::path make_temp_path(const fs::path& target) {
    auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
    auto stem = target.filename().string();
    auto parent = target.parent_path();
    return parent / ("." + stem + ".tmp" + std::to_string(ts));
}
} // namespace

Result<void, Error> atomic_write_file(const fs::path& final_path,
    const void* data, std::size_t size) {
    auto tmp = make_temp_path(final_path);

#if defined(_WIN32)
    // Windows: use CreateFile with CREATE_NEW for exclusive creation
    HANDLE h = CreateFileW(tmp.wstring().c_str(),
        GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return XBOX_IO_ERROR(FileOpenFailed,
            "CreateFile (temp) failed: " + std::to_string(GetLastError()));
    }

    if (size > 0) {
        const auto* p = static_cast<const u8*>(data);
        std::size_t total = 0;
        while (total < size) {
            DWORD to_write = static_cast<DWORD>(std::min<std::size_t>(size - total, 0x40000000));
            DWORD written = 0;
            if (!WriteFile(h, p + total, to_write, &written, nullptr) || written != to_write) {
                CloseHandle(h);
                std::error_code ec;
                fs::remove(tmp, ec);
                return XBOX_IO_ERROR(FileWriteFailed,
                    "WriteFile failed: " + std::to_string(GetLastError()));
            }
            total += written;
        }
    }

    // Flush to disk for durability
    if (!FlushFileBuffers(h)) {
        CloseHandle(h);
        std::error_code ec;
        fs::remove(tmp, ec);
        return XBOX_IO_ERROR(FileWriteFailed,
            "FlushFileBuffers failed: " + std::to_string(GetLastError()));
    }
    CloseHandle(h);

    // Atomic rename with replace
    auto r = atomic_replace(tmp, final_path);
    if (!r.is_ok()) {
        std::error_code ec;
        fs::remove(tmp, ec);
        return std::move(r).error();
    }
#else
    // POSIX: use open with O_CREAT | O_EXCL for exclusive creation
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        return XBOX_IO_ERROR(FileOpenFailed,
            std::string("open (temp) failed: ") + std::strerror(errno));
    }

    if (size > 0) {
        const auto* p = static_cast<const u8*>(data);
        std::size_t total = 0;
        while (total < size) {
            ssize_t n = ::write(fd, p + total, size - total);
            if (n < 0) {
                if (errno == EINTR) continue;
                int e = errno;
                ::close(fd);
                std::error_code ec;
                fs::remove(tmp, ec);
                return XBOX_IO_ERROR(FileWriteFailed,
                    std::string("write failed: ") + std::strerror(e));
            }
            total += static_cast<std::size_t>(n);
        }
    }

    // fsync for durability
    if (::fsync(fd) != 0) {
        ::close(fd);
        std::error_code ec;
        fs::remove(tmp, ec);
        return XBOX_IO_ERROR(FileWriteFailed,
            std::string("fsync failed: ") + std::strerror(errno));
    }
    ::close(fd);

    // Atomic rename
    auto r = atomic_replace(tmp, final_path);
    if (!r.is_ok()) {
        std::error_code ec;
        fs::remove(tmp, ec);
        return std::move(r).error();
    }

    // fsync parent directory for rename durability
    auto parent = final_path.parent_path();
    if (!parent.empty()) {
        int dir_fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dir_fd >= 0) {
            (void)::fsync(dir_fd);
            ::close(dir_fd);
        }
    }
#endif
    return {};
}

Result<RemovalReport, Error> recursive_remove_all(const fs::path& p) {
    RemovalReport report;
    std::error_code ec;

    if (!fs::exists(p, ec)) {
        return report;  // nothing to remove
    }

    // We iterate manually so we can collect per-file failures
    for (auto it = fs::directory_iterator(p, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const auto& entry = *it;
        if (entry.is_directory(ec)) {
            auto sub = recursive_remove_all(entry.path());
            if (!sub.is_ok()) {
                report.failed_paths.push_back(entry.path().string() + ": " + sub.error().message());
            } else {
                report.removed_count += sub.value().removed_count;
            }
        } else {
            if (fs::remove(entry.path(), ec); ec) {
                report.failed_paths.push_back(entry.path().string() + ": " + ec.message());
            } else {
                ++report.removed_count;
            }
        }
    }

    // Now remove the (hopefully empty) directory itself
    if (fs::remove(p, ec); ec) {
        report.failed_paths.push_back(p.string() + ": " + ec.message());
    } else {
        ++report.removed_count;
    }
    return report;
}

std::optional<u64> file_size(const fs::path& p) noexcept {
    std::error_code ec;
    auto s = fs::file_size(p, ec);
    if (ec) return std::nullopt;
    return s;
}

std::string display_path(const fs::path& p) {
    std::string s = p.generic_string();
    return s;
}

std::string format_title_id(u32 title_id) {
    return str::format_hex_u32(title_id, /*uppercase=*/true, /*pad=*/true);
}

std::string format_content_type(u32 content_type) {
    return str::format_hex_u32(content_type, /*uppercase=*/true, /*pad=*/true);
}

std::string format_content_id(const u8* id, std::size_t len) {
    return str::to_hex(id, len);
}

bool is_writable(const fs::path& dir) noexcept {
    try {
        if (!fs::exists(dir)) return false;
        auto tmp = dir / ".xbox_write_test";
        std::ofstream out(tmp);
        if (!out) return false;
        out << "x";
        out.close();
        std::error_code ec;
        fs::remove(tmp, ec);
        return true;
    } catch (...) {
        return false;
    }
}

fs::path expand_home(std::string_view p) {
    if (p.empty()) return fs::path(std::string(p));
    if (p[0] != '~') return fs::path(std::string(p));

    // Cross-platform home directory resolution
    std::string home;
#if defined(_WIN32)
    if (const char* h = std::getenv("USERPROFILE")) home = h;
    else if (const char* h = std::getenv("HOMEDRIVE")) {
        if (const char* p2 = std::getenv("HOMEPATH")) {
            home = std::string(h) + p2;
        }
    }
#else
    if (const char* h = std::getenv("HOME")) home = h;
#endif
    if (home.empty()) return fs::path(std::string(p));

    if (p.size() == 1) return fs::path(home);
    if (p[1] == '/' || p[1] == '\\') {
        return fs::path(home) / fs::path(std::string(p.substr(2)));
    }
    // ~user not supported - return as-is
    return fs::path(std::string(p));
}

} // namespace xbox::path
