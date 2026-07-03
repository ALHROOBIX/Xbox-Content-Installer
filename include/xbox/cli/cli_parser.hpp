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

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xbox::cli {

// A parsed CLI invocation: command + flags + positional args.
struct ParsedArgs {
    std::string command;                              // e.g. "install"
    std::map<std::string, std::string> flags;         // --flag -> value (or "true")
    std::vector<std::string> positionals;             // positional arguments
    bool help_requested{false};
    bool version_requested{false};
    bool json_output{false};
    bool dry_run{false};
    bool no_mmap{false};                              // --no-mmap: force buffer reading
    bool extract_svod_files{false};                   // --extract-svod: extract SVOD files (default: just copy .data)
    int verbose{0};                                   // 0 = info, 1 = debug, 2 = trace
    std::size_t threads{4};                           // --threads N
    std::string content_root{};                       // --content-root PATH (empty = use saved/default)
    std::string conflict_policy{"overwrite"};          // --on-conflict skip|overwrite|rename|fail
    std::string xuid_override{};                      // --xuid XUID (force install to specific profile)
};

// Specification of a subcommand (for help generation & validation).
struct CommandSpec {
    std::string name;
    std::string description;
    std::vector<std::string> aliases;
    std::vector<std::string> positional_args;          // names (for help text)
    std::vector<std::pair<std::string, std::string>> flags;  // (name, description)
    bool accepts_variadic_positionals{false};
};

// Get all command specs (for help text).
[[nodiscard]] const std::vector<CommandSpec>& command_specs();

// Parse argv into a ParsedArgs structure.
[[nodiscard]] Result<ParsedArgs, Error> parse_args(int argc, char** argv);

// Format the help text for a given command (or general help if empty).
[[nodiscard]] std::string format_help(std::string_view command = "");

// Format the version banner.
[[nodiscard]] std::string format_version();

} // namespace xbox::cli
