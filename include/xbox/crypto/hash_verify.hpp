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
#include "xbox/crypto/sha1.hpp"

#include <span>

namespace xbox::stfs {
class StfsReader;  // forward decl
} // namespace xbox::stfs

namespace xbox::crypto {

// Outcome of a single block verification.
struct BlockVerifyResult {
    u32   block_index{0};
    bool  ok{false};
    u8    stored_status{0};
    SHA1::Digest stored_hash{};
    SHA1::Digest computed_hash{};
};

// Verify a single block against its hash entry.
// `hash_table_data` must point to the start of the hash table that contains
// the entry for `block_index` (170-entry table).
[[nodiscard]] BlockVerifyResult verify_block(
    u32 block_index_in_table,
    std::span<const byte> block_data,
    std::span<const byte> hash_table_data);

// Pretty-print the verification result for logging.
[[nodiscard]] std::string format_verify_result(const BlockVerifyResult& r);

} // namespace xbox::crypto
