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
#include "xbox/cli/cli_parser.hpp"
#include "xbox/installer/installer.hpp"
#include "xbox/installer/path_resolver.hpp"

#include <string>

namespace xbox::cli {

// Exit codes follow BSD sysexits.h conventions.
namespace exit_code {
    constexpr int OK            = 0;
    constexpr int USAGE         = 64;  // command line usage error
    constexpr int DATAERR       = 65;  // data format error (e.g. invalid STFS)
    constexpr int NOINPUT       = 66;  // cannot open input
    constexpr int UNAVAILABLE   = 69;  // service unavailable
    constexpr int SOFTWARE      = 70;  // internal software error
    constexpr int CANTCREAT     = 73;  // can't create (user) output
    constexpr int IOERR         = 74;  // input/output error
    constexpr int TEMPFAIL      = 75;  // temp failure; user is invited to retry
    constexpr int CONFIG        = 78;  // configuration error
}

// Dispatch to the right subcommand based on ParsedArgs.
[[nodiscard]] int dispatch(ParsedArgs args);

// Individual subcommands
[[nodiscard]] int cmd_install(const ParsedArgs& args);
[[nodiscard]] int cmd_uninstall(const ParsedArgs& args);
[[nodiscard]] int cmd_list(const ParsedArgs& args);
[[nodiscard]] int cmd_info(const ParsedArgs& args);
[[nodiscard]] int cmd_verify(const ParsedArgs& args);
[[nodiscard]] int cmd_disable(const ParsedArgs& args);
[[nodiscard]] int cmd_enable(const ParsedArgs& args);
[[nodiscard]] int cmd_search(const ParsedArgs& args);
[[nodiscard]] int cmd_saves(const ParsedArgs& args);
[[nodiscard]] int cmd_export_save(const ParsedArgs& args);

// Print a structured message in either human-readable or JSON form.
void emit_result(const ParsedArgs& args, bool success,
                 std::string_view message,
                 const std::string& json_payload = "");

} // namespace xbox::cli
