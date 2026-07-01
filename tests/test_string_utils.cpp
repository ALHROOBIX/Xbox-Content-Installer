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
#include "test_framework.hpp"
#include "xbox/utils/string_utils.hpp"

using namespace xbox;
using namespace xbox::str;

TEST(StringUtils_Trim) {
    EXPECT_EQ(trim("  hello  "), "hello");
    EXPECT_EQ(trim("\t\thello\n"), "hello");
    EXPECT_EQ(trim("hello"), "hello");
    EXPECT_EQ(trim(""), "");
    EXPECT_EQ(trim("   "), "");
}

TEST(StringUtils_TrimView) {
    EXPECT_EQ(trim_view("  hello  "), "hello");
    EXPECT_EQ(trim_view("hello"), "hello");
}

TEST(StringUtils_Case) {
    EXPECT_EQ(to_lower("Hello World"), "hello world");
    EXPECT_EQ(to_upper("Hello World"), "HELLO WORLD");
    EXPECT_TRUE(iequals("Hello", "HELLO"));
    EXPECT_TRUE(iequals("ABC", "abc"));
    EXPECT_FALSE(iequals("ABC", "abd"));
}

TEST(StringUtils_StartsEndsWith) {
    EXPECT_TRUE(starts_with("hello world", "hello"));
    EXPECT_FALSE(starts_with("hello world", "world"));
    EXPECT_TRUE(ends_with("hello world", "world"));
    EXPECT_FALSE(ends_with("hello world", "hello"));
    EXPECT_TRUE(starts_with("hello", ""));   // empty prefix always matches
    EXPECT_TRUE(ends_with("hello", ""));
}

TEST(StringUtils_Split) {
    auto parts = split("a/b/c", '/');
    EXPECT_EQ(parts.size(), 3u);
    EXPECT_EQ(parts[0], "a");
    EXPECT_EQ(parts[1], "b");
    EXPECT_EQ(parts[2], "c");

    auto parts2 = split("hello", '/');
    EXPECT_EQ(parts2.size(), 1u);
    EXPECT_EQ(parts2[0], "hello");

    auto parts3 = split("", '/');
    EXPECT_EQ(parts3.size(), 1u);
    EXPECT_EQ(parts3[0], "");
}

TEST(StringUtils_Hex) {
    EXPECT_EQ(to_hex("\x01\x02\x03", 3), "010203");
    EXPECT_EQ(to_hex("\xFF", 1), "ff");

    auto parsed = from_hex("010203");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->size(), 3u);
    EXPECT_EQ((*parsed)[0], 1);
    EXPECT_EQ((*parsed)[1], 2);
    EXPECT_EQ((*parsed)[2], 3);

    EXPECT_FALSE(from_hex("xyz").has_value());
    EXPECT_FALSE(from_hex("123").has_value());  // odd length
}

TEST(StringUtils_HexU32) {
    EXPECT_EQ(format_hex_u32(0x415607ED), "415607ED");
    EXPECT_EQ(format_hex_u32(0xFF), "000000FF");
    EXPECT_EQ(format_hex_u32(0xFF, false), "000000ff");
    EXPECT_EQ(format_hex_u32(0xFF, true, false), "FF");
}

TEST(StringUtils_ParseHexU32) {
    EXPECT_EQ(parse_hex_u32("415607ED"), 0x415607EDu);
    EXPECT_EQ(parse_hex_u32("415607ed"), 0x415607EDu);
    EXPECT_EQ(parse_hex_u32("FF"), 0xFFu);
    EXPECT_FALSE(parse_hex_u32("xyz").has_value());
    EXPECT_FALSE(parse_hex_u32("").has_value());
    EXPECT_FALSE(parse_hex_u32("123456789").has_value());  // too long
}

TEST(StringUtils_Utf16BE) {
    // "Hello" in UTF-16 BE
    u8 buf[] = {0x00, 'H', 0x00, 'e', 0x00, 'l', 0x00, 'l', 0x00, 'o', 0x00, 0x00};
    std::string result = utf16be_cstr_to_utf8(buf, sizeof(buf));
    EXPECT_EQ(result, "Hello");
}

TEST(StringUtils_Utf16BECyrillic) {
    // Russian "Привет" (Hello) in UTF-16 BE
    // П = U+041F, р = U+0440, и = U+0438, в = U+0432, е = U+0435, т = U+0442
    u8 buf[] = {
        0x04, 0x1F, 0x04, 0x40, 0x04, 0x38, 0x04, 0x32,
        0x04, 0x35, 0x04, 0x42,
        0x00, 0x00
    };
    std::string result = utf16be_cstr_to_utf8(buf, sizeof(buf));
    EXPECT_EQ(result, "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");
}

TEST(StringUtils_SanitizeFilename) {
    EXPECT_EQ(sanitize_filename("hello.txt"), "hello.txt");
    EXPECT_EQ(sanitize_filename("hello/world"), "hello_world");
    EXPECT_EQ(sanitize_filename("hello\\world"), "hello_world");
    EXPECT_EQ(sanitize_filename("hello:world"), "hello_world");
    EXPECT_EQ(sanitize_filename("hello*world?"), "hello_world_");
    EXPECT_EQ(sanitize_filename("hello."), "hello");    // trailing dot removed
    EXPECT_EQ(sanitize_filename("hello "), "hello");    // trailing space removed
    EXPECT_EQ(sanitize_filename(""), "_");
    EXPECT_EQ(sanitize_filename("..."), "_");
}

TEST(StringUtils_ParseInt) {
    EXPECT_EQ(parse_int<int>("42").value_or(-1), 42);
    EXPECT_EQ(parse_int<int>("-42").value_or(0), -42);
    EXPECT_EQ(parse_int<u32>("FF", 16).value_or(0), 255u);
    EXPECT_FALSE(parse_int<int>("abc").has_value());
    EXPECT_FALSE(parse_int<int>("").has_value());
    EXPECT_FALSE(parse_int<int>("123abc").has_value());
}
