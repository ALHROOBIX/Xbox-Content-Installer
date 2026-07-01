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

#include "xbox/core/types.hpp"

#include <bit>
#include <cstring>
#include <type_traits>

namespace xbox::endian {

// ---------------------------------------------------------------------------
// Platform endianness
// ---------------------------------------------------------------------------
#if __cplusplus >= 202002L
constexpr bool is_little_endian() noexcept { return std::endian::native == std::endian::little; }
constexpr bool is_big_endian()    noexcept { return std::endian::native == std::endian::big;    }
#else
// Fallback for non-conforming compilers
inline bool is_little_endian() noexcept {
    const u16 test = 0x0001;
    return *reinterpret_cast<const u8*>(&test) == 0x01;
}
inline bool is_big_endian() noexcept { return !is_little_endian(); }
#endif

// ---------------------------------------------------------------------------
// Byte-swap primitives (constexpr in C++20)
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr u16 byteswap16(u16 v) noexcept {
    return static_cast<u16>((v >> 8) | (v << 8));
}

[[nodiscard]] constexpr u32 byteswap32(u32 v) noexcept {
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) <<  8) |
           ((v & 0x00FF0000u) >>  8) |
           ((v & 0xFF000000u) >> 24);
}

[[nodiscard]] constexpr u64 byteswap64(u64 v) noexcept {
    return ((v & 0x00000000000000FFull) << 56) |
           ((v & 0x000000000000FF00ull) << 40) |
           ((v & 0x0000000000FF0000ull) << 24) |
           ((v & 0x00000000FF000000ull) <<  8) |
           ((v & 0x000000FF00000000ull) >>  8) |
           ((v & 0x0000FF0000000000ull) >> 24) |
           ((v & 0x00FF000000000000ull) >> 40) |
           ((v & 0xFF00000000000000ull) >> 56);
}

// ---------------------------------------------------------------------------
// Safe unaligned readers (memcpy-based; never UB)
// ---------------------------------------------------------------------------
template <typename T>
[[nodiscard]] inline T read_unaligned(const void* p) noexcept {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    T value;
    std::memcpy(&value, p, sizeof(T));
    return value;
}

// Read a little-endian unsigned integer of width N from a byte buffer.
// Bytes beyond the type width are zero-padded (for int24 support).
template <typename T>
[[nodiscard]] inline T read_le(const void* p, std::size_t byte_count = sizeof(T)) noexcept {
    T value = 0;
    const auto* bytes = static_cast<const u8*>(p);
    for (std::size_t i = 0; i < byte_count; ++i) {
        value |= static_cast<T>(bytes[i]) << (8 * i);
    }
    return value;
}

template <typename T>
[[nodiscard]] inline T read_be(const void* p, std::size_t byte_count = sizeof(T)) noexcept {
    T value = 0;
    const auto* bytes = static_cast<const u8*>(p);
    for (std::size_t i = 0; i < byte_count; ++i) {
        value = (value << 8) | static_cast<T>(bytes[i]);
    }
    return value;
}

// Concrete typed readers for the most common widths.
[[nodiscard]] inline u16 read_u16_le(const void* p) noexcept { return read_le<u16>(p); }
[[nodiscard]] inline u16 read_u16_be(const void* p) noexcept { return read_be<u16>(p); }
[[nodiscard]] inline u32 read_u32_le(const void* p) noexcept { return read_le<u32>(p); }
[[nodiscard]] inline u32 read_u32_be(const void* p) noexcept { return read_be<u32>(p); }
[[nodiscard]] inline u64 read_u64_le(const void* p) noexcept { return read_le<u64>(p); }
[[nodiscard]] inline u64 read_u64_be(const void* p) noexcept { return read_be<u64>(p); }

// ---------------------------------------------------------------------------
// 24-bit integer readers (used heavily by STFS block indices)
//
// Spec note: STFS uses 3-byte little-endian integers for "Number of blocks
// allocated" and "Starting block number" inside file-listing entries, but
// uses BIG-endian for the path indicator (a 2-byte short).
// ---------------------------------------------------------------------------
[[nodiscard]] inline u32 read_u24_le(const void* p) noexcept { return read_le<u32>(p, 3); }
[[nodiscard]] inline u32 read_u24_be(const void* p) noexcept { return read_be<u32>(p, 3); }

// Signed variants
[[nodiscard]] inline i32 read_i24_le(const void* p) noexcept {
    u32 v = read_u24_le(p);
    // Sign-extend if bit 23 is set
    if (v & 0x800000u) v |= 0xFF000000u;
    return static_cast<i32>(v);
}

[[nodiscard]] inline i32 read_i24_be(const void* p) noexcept {
    u32 v = read_u24_be(p);
    if (v & 0x800000u) v |= 0xFF000000u;
    return static_cast<i32>(v);
}

// ---------------------------------------------------------------------------
// Host-order conversions: returns the value as stored if the host matches
// the requested endianness, otherwise byteswaps.
// ---------------------------------------------------------------------------
[[nodiscard]] inline u16 le_to_host(u16 v) noexcept { return is_little_endian() ? v : byteswap16(v); }
[[nodiscard]] inline u32 le_to_host(u32 v) noexcept { return is_little_endian() ? v : byteswap32(v); }
[[nodiscard]] inline u64 le_to_host(u64 v) noexcept { return is_little_endian() ? v : byteswap64(v); }

[[nodiscard]] inline u16 be_to_host(u16 v) noexcept { return is_big_endian() ? v : byteswap16(v); }
[[nodiscard]] inline u32 be_to_host(u32 v) noexcept { return is_big_endian() ? v : byteswap32(v); }
[[nodiscard]] inline u64 be_to_host(u64 v) noexcept { return is_big_endian() ? v : byteswap64(v); }

// ---------------------------------------------------------------------------
// Convenience: read N bytes as host-order hex string (for logging)
// ---------------------------------------------------------------------------
[[nodiscard]] std::string to_hex(const void* data, std::size_t len);
[[nodiscard]] std::string to_hex_upper(u32 v) noexcept;
[[nodiscard]] std::string to_hex_lower(u32 v) noexcept;
[[nodiscard]] std::string to_hex_upper(u64 v) noexcept;
[[nodiscard]] std::string to_hex_lower(u64 v) noexcept;

} // namespace xbox::endian
