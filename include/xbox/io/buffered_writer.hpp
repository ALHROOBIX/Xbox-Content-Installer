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
#include <span>
#include <vector>

namespace xbox::io {

namespace fs = std::filesystem;

class BufferedWriter {
public:
    explicit BufferedWriter(std::size_t buffer_size = 256 * 1024);
    ~BufferedWriter();

    BufferedWriter(const BufferedWriter&) = delete;
    BufferedWriter& operator=(const BufferedWriter&) = delete;
    BufferedWriter(BufferedWriter&&) noexcept;
    BufferedWriter& operator=(BufferedWriter&&) noexcept;

    // Open a file for writing (creates or truncates).
    [[nodiscard]] Result<void, Error> open(const fs::path& p);

    // Write data; may flush internally if buffer fills.
    [[nodiscard]] Result<void, Error> write(const void* data, std::size_t count);
    [[nodiscard]] Result<void, Error> write(std::span<const byte> data);

    // Flush the internal buffer to the OS. Does NOT fsync.
    [[nodiscard]] Result<void, Error> flush();

    // Force OS write to disk (fsync / FlushFileBuffers).
    [[nodiscard]] Result<void, Error> sync();

    // Close the writer (flushes + closes). Idempotent.
    [[nodiscard]] Result<void, Error> close();

    [[nodiscard]] bool is_open() const noexcept { return fd_ != invalid_fd; }
    [[nodiscard]] u64  bytes_written() const noexcept { return total_bytes_; }

private:
#if defined(_WIN32)
    static constexpr int invalid_fd = -1;
    void* file_handle_{nullptr};
    int   fd_{invalid_fd};  // we still use fd_-1 sentinel for "not open"
#else
    static constexpr int invalid_fd = -1;
    int  fd_{invalid_fd};
#endif
    std::vector<u8> buffer_;
    std::size_t     buffer_pos_{0};
    u64             total_bytes_{0};
    fs::path        path_;
};

} // namespace xbox::io
