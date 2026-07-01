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

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>

namespace xbox::installer {

namespace fs = std::filesystem;

enum class ConflictPolicy : u8 {
    Overwrite,
    Skip,
    Rename,
    Fail,
    Prompt,
};

[[nodiscard]] std::string_view conflict_policy_name(ConflictPolicy p) noexcept;

// User-prompt callback for the Prompt policy.
// Receives: existing_path, proposed_new_path
// Returns:  true = overwrite, false = skip
using PromptCallback = std::function<bool(const fs::path& existing)>;

// Apply the conflict policy to an existing directory.
// Returns:
//   - On Overwrite: deletes the existing dir, returns true (proceed).
//   - On Skip: returns false (do not proceed).
//   - On Rename: renames to <path>.bak-YYYYMMDD-HHMMSS, returns true.
//   - On Fail: returns an error.
//   - On Prompt: invokes the callback; if user says yes, falls through to Overwrite.
[[nodiscard]] Result<bool, Error> resolve_conflict(
    const fs::path& existing_dir,
    ConflictPolicy policy,
    PromptCallback prompt = nullptr);

} // namespace xbox::installer
