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
#include "xbox/crypto/sha1.hpp"

#include "xbox/core/endian.hpp"

#include <cstring>

namespace xbox::crypto {

// SHA-1 initial hash values (FIPS 180-4 section 5.3.1)
static constexpr u32 H0 = 0x67452301;
static constexpr u32 H1 = 0xEFCDAB89;
static constexpr u32 H2 = 0x98BADCFE;
static constexpr u32 H3 = 0x10325476;
static constexpr u32 H4 = 0xC3D2E1F0;

// SHA-1 round constants (FIPS 180-4 section 4.1.1)
static constexpr u32 K0 = 0x5A827999;  // rounds  0-19
static constexpr u32 K1 = 0x6ED9EBA1;  // rounds 20-39
static constexpr u32 K2 = 0x8F1BBCDC;  // rounds 40-59
static constexpr u32 K3 = 0xCA62C1D6;  // rounds 60-79

[[nodiscard]] static inline u32 rotl(u32 x, int n) noexcept {
    return (x << n) | (x >> (32 - n));
}

void SHA1::init() noexcept {
    h0_ = H0;
    h1_ = H1;
    h2_ = H2;
    h3_ = H3;
    h4_ = H4;
    bit_count_ = 0;
    buffer_len_ = 0;
    finalized_ = false;
}

void SHA1::update(const void* data, std::size_t len) noexcept {
    if (finalized_) {
        // Re-initialize if update() is called after final()
        init();
    }

    auto* p = static_cast<const u8*>(data);
    bit_count_ += static_cast<u64>(len) * 8;

    // If we have leftover data in the buffer, fill it first
    if (buffer_len_ > 0) {
        std::size_t need = BLOCK_SIZE - buffer_len_;
        if (len < need) {
            std::memcpy(buffer_ + buffer_len_, p, len);
            buffer_len_ += len;
            return;
        }
        std::memcpy(buffer_ + buffer_len_, p, need);
        p += need;
        len -= need;
        process_block(buffer_);
        buffer_len_ = 0;
    }

    // Process full blocks directly
    while (len >= BLOCK_SIZE) {
        process_block(p);
        p += BLOCK_SIZE;
        len -= BLOCK_SIZE;
    }

    // Stash the tail
    if (len > 0) {
        std::memcpy(buffer_, p, len);
        buffer_len_ = len;
    }
}

SHA1::Digest SHA1::final() noexcept {
    if (finalized_) {
        // Already finalized - return the cached digest (in h-vars)
    } else {
        // Append the padding: 0x80, then zeros, then 64-bit big-endian length
        buffer_[buffer_len_++] = 0x80;

        // If not enough room for 8-byte length, pad with zeros and process
        if (buffer_len_ > BLOCK_SIZE - 8) {
            while (buffer_len_ < BLOCK_SIZE) buffer_[buffer_len_++] = 0;
            process_block(buffer_);
            buffer_len_ = 0;
        }

        while (buffer_len_ < BLOCK_SIZE - 8) buffer_[buffer_len_++] = 0;

        // Append 64-bit big-endian bit count
        u64 bc = bit_count_;
        for (int i = 7; i >= 0; --i) {
            buffer_[buffer_len_++] = static_cast<u8>((bc >> (8 * i)) & 0xFF);
        }

        process_block(buffer_);
        finalized_ = true;
    }

    Digest out{};
    out[ 0] = static_cast<u8>(h0_ >> 24);
    out[ 1] = static_cast<u8>(h0_ >> 16);
    out[ 2] = static_cast<u8>(h0_ >>  8);
    out[ 3] = static_cast<u8>(h0_      );
    out[ 4] = static_cast<u8>(h1_ >> 24);
    out[ 5] = static_cast<u8>(h1_ >> 16);
    out[ 6] = static_cast<u8>(h1_ >>  8);
    out[ 7] = static_cast<u8>(h1_      );
    out[ 8] = static_cast<u8>(h2_ >> 24);
    out[ 9] = static_cast<u8>(h2_ >> 16);
    out[10] = static_cast<u8>(h2_ >>  8);
    out[11] = static_cast<u8>(h2_      );
    out[12] = static_cast<u8>(h3_ >> 24);
    out[13] = static_cast<u8>(h3_ >> 16);
    out[14] = static_cast<u8>(h3_ >>  8);
    out[15] = static_cast<u8>(h3_      );
    out[16] = static_cast<u8>(h4_ >> 24);
    out[17] = static_cast<u8>(h4_ >> 16);
    out[18] = static_cast<u8>(h4_ >>  8);
    out[19] = static_cast<u8>(h4_      );
    return out;
}

void SHA1::process_block(const u8* block) noexcept {
    // Read the 16 big-endian 32-bit words
    u32 w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<u32>(block[i*4 + 0]) << 24) |
               (static_cast<u32>(block[i*4 + 1]) << 16) |
               (static_cast<u32>(block[i*4 + 2]) <<  8) |
               (static_cast<u32>(block[i*4 + 3])      );
    }
    // Extend to 80 words
    for (int i = 16; i < 80; ++i) {
        w[i] = rotl(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }

    u32 a = h0_, b = h1_, c = h2_, d = h3_, e = h4_;

    for (int i = 0; i < 80; ++i) {
        u32 f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = K0;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = K1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = K2;
        } else {
            f = b ^ c ^ d;
            k = K3;
        }

        u32 temp = rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl(b, 30);
        b = a;
        a = temp;
    }

    h0_ += a;
    h1_ += b;
    h2_ += c;
    h3_ += d;
    h4_ += e;
}

bool SHA1::equal(const Digest& a, const Digest& b) noexcept {
    u8 diff = 0;
    for (std::size_t i = 0; i < DIGEST_SIZE; ++i) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

std::string SHA1::to_hex(const Digest& d) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out(DIGEST_SIZE * 2, '0');
    for (std::size_t i = 0; i < DIGEST_SIZE; ++i) {
        out[i*2 + 0] = hex[(d[i] >> 4) & 0xF];
        out[i*2 + 1] = hex[d[i] & 0xF];
    }
    return out;
}

} // namespace xbox::crypto
