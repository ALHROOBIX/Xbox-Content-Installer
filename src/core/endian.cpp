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
#include "xbox/core/endian.hpp"

#include <cstdio>

namespace xbox::endian {

std::string to_hex(const void* data, std::size_t len) {
    static constexpr char hex[] = "0123456789abcdef";
    const auto* bytes = static_cast<const u8*>(data);
    std::string out(len * 2, '0');
    for (std::size_t i = 0; i < len; ++i) {
        out[i*2 + 0] = hex[(bytes[i] >> 4) & 0xF];
        out[i*2 + 1] = hex[bytes[i] & 0xF];
    }
    return out;
}

std::string to_hex_upper(u32 v) noexcept {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08X", v);
    return buf;
}

std::string to_hex_lower(u32 v) noexcept {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", v);
    return buf;
}

std::string to_hex_upper(u64 v) noexcept {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(v));
    return buf;
}

std::string to_hex_lower(u64 v) noexcept {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

} // namespace xbox::endian
