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
#include "xbox/core/endian.hpp"

#include <cstring>

using namespace xbox;
using namespace xbox::endian;

TEST(Endian_Byteswap16) {
    EXPECT_EQ(byteswap16(0x1234), 0x3412);
    EXPECT_EQ(byteswap16(0x0000), 0x0000);
    EXPECT_EQ(byteswap16(0xFFFF), 0xFFFF);
    EXPECT_EQ(byteswap16(0xABCD), 0xCDAB);
}

TEST(Endian_Byteswap32) {
    EXPECT_EQ(byteswap32(0x12345678u), 0x78563412u);
    EXPECT_EQ(byteswap32(0x00000000u), 0x00000000u);
    EXPECT_EQ(byteswap32(0xFFFFFFFFu), 0xFFFFFFFFu);
    EXPECT_EQ(byteswap32(0xDEADBEEFu), 0xEFBEADDEu);
}

TEST(Endian_Byteswap64) {
    EXPECT_EQ(byteswap64(0x1122334455667788ull), 0x8877665544332211ull);
    EXPECT_EQ(byteswap64(0x0000000000000000ull), 0x0000000000000000ull);
    EXPECT_EQ(byteswap64(0xFFFFFFFFFFFFFFFFull), 0xFFFFFFFFFFFFFFFFull);
}

TEST(Endian_ReadU16) {
    u8 buf[] = {0x12, 0x34};
    EXPECT_EQ(read_u16_le(buf), 0x3412u);
    EXPECT_EQ(read_u16_be(buf), 0x1234u);
}

TEST(Endian_ReadU32) {
    u8 buf[] = {0x12, 0x34, 0x56, 0x78};
    EXPECT_EQ(read_u32_le(buf), 0x78563412u);
    EXPECT_EQ(read_u32_be(buf), 0x12345678u);
}

TEST(Endian_ReadU24) {
    // 24-bit value 0x123456
    u8 buf[] = {0x12, 0x34, 0x56};
    // Little-endian: 0x563412
    EXPECT_EQ(read_u24_le(buf), 0x563412u);
    // Big-endian:    0x123456
    EXPECT_EQ(read_u24_be(buf), 0x123456u);
}

TEST(Endian_ReadI24_Negative) {
    // -1 in 24-bit signed (LE)
    u8 buf[] = {0xFF, 0xFF, 0xFF};
    EXPECT_EQ(read_i24_le(buf), -1);
    EXPECT_EQ(read_i24_be(buf), -1);

    // -128 in 24-bit signed
    u8 buf2[] = {0x80, 0x00, 0x00};  // BE: -8388608, LE: 128
    EXPECT_EQ(read_i24_be(buf2), -8388608);
    // LE: 0x000080 = 128
    EXPECT_EQ(read_i24_le(buf2), 128);
}

TEST(Endian_ReadUnaligned) {
    u8 buf[] = {0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0x00, 0x00};
    // Reading at offset 2 (potentially unaligned)
    u32 v = read_unaligned<u32>(buf + 2);
    // On little-endian: 0x78563412
    if (is_little_endian()) {
        EXPECT_EQ(v, 0x78563412u);
    } else {
        EXPECT_EQ(v, 0x12345678u);
    }
}

TEST(Endian_HexFormatting) {
    // to_hex_upper uses %08X format - 8 chars zero-padded
    EXPECT_EQ(to_hex_upper(0xFFu), "000000FF");
    EXPECT_EQ(to_hex_lower(0xFFu), "000000ff");
    EXPECT_EQ(to_hex_upper(0xDEADBEEFu), "DEADBEEF");  // 8 chars, no padding needed
    EXPECT_EQ(to_hex_lower(0xDEADBEEFu), "deadbeef");
    EXPECT_EQ(to_hex_upper(0u), "00000000");
}

TEST(Endian_Nested) {
    // Verify that LE/BE conversions are consistent
    u8 buf[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    u32 a = read_u32_be(buf);
    u32 b = read_u32_be(buf + 4);

    EXPECT_EQ(a, 0x01020304u);
    EXPECT_EQ(b, 0x05060708u);

    // u64 read should combine both
    u64 c = read_u64_be(buf);
    EXPECT_EQ(c, 0x0102030405060708ull);
}
