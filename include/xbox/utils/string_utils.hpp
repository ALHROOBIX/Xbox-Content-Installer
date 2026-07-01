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

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace xbox::str {

// ----- Whitespace trimming -----
[[nodiscard]] inline std::string trim_left(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
        [](unsigned char c) { return !std::isspace(c); }));
    return s;
}

[[nodiscard]] inline std::string trim_right(std::string s) {
    s.erase(std::find_if(s.rbegin(), s.rend(),
        [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
    return s;
}

[[nodiscard]] inline std::string trim(std::string s) {
    return trim_right(trim_left(std::move(s)));
}

[[nodiscard]] inline std::string_view trim_view(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

// ----- Case conversion -----
[[nodiscard]] inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

[[nodiscard]] inline std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

[[nodiscard]] inline bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
        std::equal(a.begin(), a.end(), b.begin(),
            [](unsigned char ca, unsigned char cb) {
                return std::tolower(ca) == std::tolower(cb);
            });
}

[[nodiscard]] inline bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] inline bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

// ----- Splitting -----
[[nodiscard]] std::vector<std::string> split(std::string_view s, char delim);
[[nodiscard]] std::vector<std::string> split(std::string_view s, std::string_view delim);

// ----- Hex parsing / formatting -----
[[nodiscard]] std::string to_hex(const void* data, std::size_t len);
[[nodiscard]] std::string to_hex(std::string_view bytes);
[[nodiscard]] std::optional<std::vector<u8>> from_hex(std::string_view s);

[[nodiscard]] std::string format_hex_u32(u32 v, bool uppercase = true, bool pad_to_8 = true);
[[nodiscard]] std::string format_hex_u64(u64 v, bool uppercase = true, bool pad_to_16 = true);

// Parse hex string of exactly 8 chars into u32 (e.g. "41560CED" -> title ID)
[[nodiscard]] std::optional<u32> parse_hex_u32(std::string_view s);
[[nodiscard]] std::optional<u64> parse_hex_u64(std::string_view s);

// ----- Integer parsing -----
template <typename T>
[[nodiscard]] std::optional<T> parse_int(std::string_view s, int base = 10) {
    T value{};
    auto* end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(s.data(), end, value, base);
    if (ec != std::errc{} || ptr != end) return std::nullopt;
    return value;
}

// ----- UTF-16 BE decoding -----
// STFS stores strings as UTF-16 BE. Convert to UTF-8 for display.
[[nodiscard]] std::string utf16be_to_utf8(const u8* data, std::size_t byte_len);
[[nodiscard]] std::string utf16be_to_utf8(std::string_view bytes);

// Read a null-terminated UTF-16 BE string from a fixed-size buffer.
[[nodiscard]] std::string utf16be_cstr_to_utf8(const u8* data, std::size_t max_byte_len);

// ----- Filename sanitization -----
// Xbox 360 file names can contain characters illegal on the host file
// system. Replace them with '_' before writing to disk.
[[nodiscard]] std::string sanitize_filename(std::string_view name);

// ----- Console width (for progress bar) -----
[[nodiscard]] int terminal_width() noexcept;

} // namespace xbox::str
