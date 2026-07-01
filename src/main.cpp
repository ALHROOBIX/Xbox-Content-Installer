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
#include "xbox/cli/cli_parser.hpp"
#include "xbox/cli/commands.hpp"
#include "xbox/core/logger.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    try {
        auto args = xbox::cli::parse_args(argc, argv);
        if (!args.is_ok()) {
            std::cerr << "Error: " << args.error().message() << "\n";
            return xbox::cli::exit_code::USAGE;
        }
        return xbox::cli::dispatch(std::move(args).value());
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return xbox::cli::exit_code::SOFTWARE;
    } catch (...) {
        std::cerr << "Fatal: unknown exception\n";
        return xbox::cli::exit_code::SOFTWARE;
    }
}
