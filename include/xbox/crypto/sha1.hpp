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

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

namespace xbox::crypto {

class SHA1 {
public:
    static constexpr std::size_t DIGEST_SIZE = 20;  // 160 bits
    static constexpr std::size_t BLOCK_SIZE  = 64;  // 512 bits

    using Digest = std::array<u8, DIGEST_SIZE>;

    SHA1() { init(); }

    // Initialize/reset the state. Safe to call multiple times.
    void init() noexcept;

    // Feed data into the hash. Can be called multiple times.
    void update(const void* data, std::size_t len) noexcept;
    void update(std::span<const byte> data) noexcept {
        update(data.data(), data.size());
    }
    void update(std::string_view data) noexcept {
        update(data.data(), data.size());
    }

    // Finalize and return the digest. State is reset.
    [[nodiscard]] Digest final() noexcept;

    // One-shot helper.
    [[nodiscard]] static Digest compute(const void* data, std::size_t len) noexcept {
        SHA1 s;
        s.update(data, len);
        return s.final();
    }
    [[nodiscard]] static Digest compute(std::string_view data) noexcept {
        return compute(data.data(), data.size());
    }
    [[nodiscard]] static Digest compute(std::span<const byte> data) noexcept {
        return compute(data.data(), data.size());
    }

    // Constant-time comparison of two digests.
    [[nodiscard]] static bool equal(const Digest& a, const Digest& b) noexcept;

    // Convert a digest to lowercase hex string.
    [[nodiscard]] static std::string to_hex(const Digest& d);

private:
    void process_block(const u8* block) noexcept;

    u32  h0_, h1_, h2_, h3_, h4_;   // hash state
    u64  bit_count_{0};             // total bits fed in
    u8   buffer_[BLOCK_SIZE];       // partial block
    std::size_t buffer_len_{0};     // bytes currently in buffer
    bool finalized_{false};
};

// Generic incremental hash context that we can use polymorphically if needed.
class HashContext {
public:
    virtual ~HashContext() = default;
    virtual void init() = 0;
    virtual void update(const void* data, std::size_t len) = 0;
    [[nodiscard]] virtual std::vector<u8> final() = 0;
    [[nodiscard]] virtual std::size_t digest_size() const = 0;
};

} // namespace xbox::crypto
