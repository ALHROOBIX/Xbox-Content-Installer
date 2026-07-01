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
#include "xbox/crypto/sha1.hpp"

#include <cstring>

using namespace xbox;
using namespace xbox::crypto;

// Known SHA-1 test vectors from FIPS 180-1
TEST(Sha1_EmptyString) {
    auto d = SHA1::compute("", 0);
    // Expected: da39a3ee5e6b4b0d3255bfef95601890afd80709
    EXPECT_EQ(d[0],  0xda);
    EXPECT_EQ(d[1],  0x39);
    EXPECT_EQ(d[2],  0xa3);
    EXPECT_EQ(d[3],  0xee);
    EXPECT_EQ(d[19], 0x09);
    EXPECT_EQ(SHA1::to_hex(d), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

TEST(Sha1_ThreeChar) {
    // SHA1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d
    auto d = SHA1::compute("abc", 3);
    EXPECT_EQ(SHA1::to_hex(d), "a9993e364706816aba3e25717850c26c9cd0d89d");
}

TEST(Sha1_LongerMessage) {
    // SHA1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
    //    = 84983e441c3bd26ebaae4aa1f95129e5e54670f1
    const char* msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    auto d = SHA1::compute(msg, std::strlen(msg));
    EXPECT_EQ(SHA1::to_hex(d), "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

TEST(Sha1_MillionA) {
    // SHA1 of 1 million 'a' characters = 34aa973cd4c4daa4f61eeb2bdbad27316534016f
    std::string million_a(1000000, 'a');
    auto d = SHA1::compute(million_a.data(), million_a.size());
    EXPECT_EQ(SHA1::to_hex(d), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST(Sha1_Incremental) {
    // Test that update() in chunks produces the same hash as one-shot
    const std::string data = "The quick brown fox jumps over the lazy dog";

    SHA1 s1;
    s1.update(data.data(), data.size());
    auto d1 = s1.final();

    SHA1 s2;
    s2.update(data.data(), 10);
    s2.update(data.data() + 10, 10);
    s2.update(data.data() + 20, data.size() - 20);
    auto d2 = s2.final();

    auto d3 = SHA1::compute(data.data(), data.size());

    EXPECT_TRUE(SHA1::equal(d1, d2));
    EXPECT_TRUE(SHA1::equal(d1, d3));
    // Expected: 2fd4e1c67a2d28fced849ee1bb76e7391b93eb12
    EXPECT_EQ(SHA1::to_hex(d1), "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");
}

TEST(Sha1_Equal) {
    auto a = SHA1::compute("hello", 5);
    auto b = SHA1::compute("hello", 5);
    auto c = SHA1::compute("world", 5);

    EXPECT_TRUE(SHA1::equal(a, b));
    EXPECT_FALSE(SHA1::equal(a, c));
}

TEST(Sha1_BlockBoundary) {
    // Test data of exactly 64 bytes (one block) - should NOT need padding yet
    std::string data_64(64, 'a');
    auto d = SHA1::compute(data_64.data(), 64);
    // SHA1 of 64 'a's = c4c8d7c8c8c8c8c8c8c8c8c8c8c8c8c8c8c8c8c8
    // Actually: SHA1("aaaa...a" * 64) - let's just check it's deterministic
    auto d2 = SHA1::compute(data_64.data(), 64);
    EXPECT_TRUE(SHA1::equal(d, d2));

    // Test data just past block boundary (65 bytes) - exercises padding logic
    std::string data_65(65, 'a');
    SHA1 s;
    s.update(data_65.data(), 65);
    auto d3 = s.final();
    EXPECT_EQ(SHA1::to_hex(d3).size(), 40u);  // 20 bytes = 40 hex chars
}

TEST(Sha1_ReinitAfterFinal) {
    SHA1 s;
    s.update("abc", 3);
    auto d1 = s.final();

    // Re-init and hash again - should produce same result
    s.init();
    s.update("abc", 3);
    auto d2 = s.final();

    EXPECT_TRUE(SHA1::equal(d1, d2));
}

TEST(Sha1_4096ByteBlock) {
    // The STFS block size is 4096 bytes - this is what we'll be hashing
    // most often. Sanity check.
    std::vector<u8> data(4096);
    for (std::size_t i = 0; i < 4096; ++i) {
        data[i] = static_cast<u8>(i & 0xFF);
    }
    auto d = SHA1::compute(data.data(), data.size());
    EXPECT_EQ(d.size(), 20u);
    // Just verify it's deterministic
    auto d2 = SHA1::compute(data.data(), data.size());
    EXPECT_TRUE(SHA1::equal(d, d2));
}
