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
#include "xbox/utils/string_utils.hpp"
#include "xbox/core/errors.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <sstream>

#if defined(_WIN32)
#  include <windows.h>
#  include <io.h>
#else
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

namespace xbox::str {

std::vector<std::string> split(std::string_view s, char delim) {
    std::vector<std::string> out;
    std::string current;
    for (char c : s) {
        if (c == delim) {
            out.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    out.push_back(std::move(current));
    return out;
}

std::vector<std::string> split(std::string_view s, std::string_view delim) {
    std::vector<std::string> out;
    std::size_t pos = 0, prev = 0;
    while ((pos = s.find(delim, prev)) != std::string_view::npos) {
        out.emplace_back(s.substr(prev, pos - prev));
        prev = pos + delim.size();
    }
    out.emplace_back(s.substr(prev));
    return out;
}

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

std::string to_hex(std::string_view bytes) {
    return to_hex(bytes.data(), bytes.size());
}

std::optional<std::vector<u8>> from_hex(std::string_view s) {
    if (s.size() % 2 != 0) return std::nullopt;
    std::vector<u8> out;
    out.reserve(s.size() / 2);
    auto hex_val = [](char c) -> std::optional<u8> {
        if (c >= '0' && c <= '9') return static_cast<u8>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<u8>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<u8>(c - 'A' + 10);
        return std::nullopt;
    };
    for (std::size_t i = 0; i < s.size(); i += 2) {
        auto hi = hex_val(s[i]);
        auto lo = hex_val(s[i + 1]);
        if (!hi || !lo) return std::nullopt;
        out.push_back(static_cast<u8>((*hi << 4) | *lo));
    }
    return out;
}

std::string format_hex_u32(u32 v, bool uppercase, bool pad_to_8) {
    char buf[16];
    const char* fmt = uppercase
        ? (pad_to_8 ? "%08X" : "%X")
        : (pad_to_8 ? "%08x" : "%x");
    std::snprintf(buf, sizeof(buf), fmt, v);
    return buf;
}

std::string format_hex_u64(u64 v, bool uppercase, bool pad_to_16) {
    char buf[32];
    const char* fmt = uppercase
        ? (pad_to_16 ? "%016llX" : "%llX")
        : (pad_to_16 ? "%016llx" : "%llx");
    std::snprintf(buf, sizeof(buf), fmt, static_cast<unsigned long long>(v));
    return buf;
}

std::optional<u32> parse_hex_u32(std::string_view s) {
    if (s.empty() || s.size() > 8) return std::nullopt;
    u32 value = 0;
    for (char c : s) {
        value <<= 4;
        if (c >= '0' && c <= '9')      value |= static_cast<u32>(c - '0');
        else if (c >= 'a' && c <= 'f') value |= static_cast<u32>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= static_cast<u32>(c - 'A' + 10);
        else return std::nullopt;
    }
    return value;
}

std::optional<u64> parse_hex_u64(std::string_view s) {
    if (s.empty() || s.size() > 16) return std::nullopt;
    u64 value = 0;
    for (char c : s) {
        value <<= 4;
        if (c >= '0' && c <= '9')      value |= static_cast<u64>(c - '0');
        else if (c >= 'a' && c <= 'f') value |= static_cast<u64>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= static_cast<u64>(c - 'A' + 10);
        else return std::nullopt;
    }
    return value;
}

// ---------------------------------------------------------------------------
// UTF-16 BE decoding (RFC 2781)
// ---------------------------------------------------------------------------
std::string utf16be_to_utf8(const u8* data, std::size_t byte_len) {
    std::string out;
    out.reserve(byte_len / 2);

    for (std::size_t i = 0; i + 1 < byte_len; i += 2) {
        u16 cp = (static_cast<u16>(data[i]) << 8) | static_cast<u16>(data[i+1]);

        // Handle surrogate pairs
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < byte_len) {
            u16 lo = (static_cast<u16>(data[i+2]) << 8) | static_cast<u16>(data[i+3]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                u32 codepoint = 0x10000 + ((static_cast<u32>(cp - 0xD800) << 10) |
                                           (static_cast<u32>(lo - 0xDC00)));
                // 4-byte UTF-8
                out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
                out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((codepoint >>  6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ( codepoint        & 0x3F)));
                i += 2;  // skip low surrogate (loop adds 2 more)
                continue;
            }
        }

        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::string utf16be_to_utf8(std::string_view bytes) {
    return utf16be_to_utf8(reinterpret_cast<const u8*>(bytes.data()), bytes.size());
}

std::string utf16be_cstr_to_utf8(const u8* data, std::size_t max_byte_len) {
    // Find the first null UTF-16 char (0x00 0x00)
    std::size_t actual = 0;
    for (; actual + 1 < max_byte_len; actual += 2) {
        if (data[actual] == 0 && data[actual + 1] == 0) break;
    }
    return utf16be_to_utf8(data, actual);
}

std::string sanitize_filename(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        // Replace illegal characters on Windows / Unix with underscore
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            out.push_back('_');
        } else if (static_cast<unsigned char>(c) < 32) {
            out.push_back('_');
        } else {
            out.push_back(c);
        }
    }
    // IMPORTANT: Do NOT trim trailing dots!
    // Xbox 360 STFS filenames legitimately contain dots as version separators.
    // Xenia preserves filenames exactly as stored in the STFS file table.
    if (out.empty()) out = "_";

    // Check for Windows reserved names (CON, PRN, AUX, NUL, COM1-9, LPT1-9)
    // These would cause failures on Windows
    std::string stem = out;
    if (auto dot = stem.find('.'); dot != std::string::npos) {
        stem = stem.substr(0, dot);
    }
    std::string stem_upper;
    stem_upper.reserve(stem.size());
    for (char c : stem) {
        stem_upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    static const std::unordered_set<std::string_view> reserved = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    if (reserved.count(stem_upper)) {
        out = "_" + out;
    }

    return out;
}

int terminal_width() noexcept {
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return 80;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(h, &csbi)) return 80;
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
#else
    if (!isatty(STDOUT_FILENO)) return 80;
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) return 80;
    return ws.ws_col;
#endif
}

} // namespace xbox::str
