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

#include "xbox/core/errors.hpp"
#include "xbox/utils/string_utils.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace xbox::cli {

namespace {
const std::vector<CommandSpec>& specs_table() {
    static const std::vector<CommandSpec> specs = {
        {
            "install", "Install one or more STFS packages (TU/DLC/etc)",
            {"i", "add"},
            {"<files...>"},
            {
                {"--content-root <path>",   "Override the content root directory"},
                {"--on-conflict <policy>",  "skip|overwrite|rename|fail (default: skip)"},
                {"--threads <N>",           "Worker threads per package (default: 4)"},
                {"--no-verify",             "Skip SHA1 verification"},
                {"--no-mmap",               "Disable mmap (use buffer reading, for NTFS/FUSE drives)"},
                {"--xuid <XUID>",          "Force install to specific profile (e.g., --xuid 0000000000000000)"},
                {"--dry-run",               "Show what would be done without writing"},
                {"--allow-unknown",         "Allow unknown content types"},
                {"--verbose, -v",           "Increase verbosity (repeat for trace)"},
                {"--json",                  "Emit machine-readable JSON output"},
            },
            true,
        },
        {
            "uninstall", "Uninstall a previously-installed package",
            {"remove", "rm"},
            {"<title_id> <content_id_prefix>"},
            {
                {"--content-root <path>",  "Override the content root directory"},
                {"--json",                 "Emit machine-readable JSON output"},
            },
            false,
        },
        {
            "list", "List installed content (optionally for a title)",
            {"ls"},
            {"[title_id]"},
            {
                {"--content-root <path>",  "Override the content root directory"},
                {"--json",                 "Emit machine-readable JSON output"},
                {"--disabled",             "Include disabled packages"},
            },
            false,
        },
        {
            "info", "Display STFS metadata for a file",
            {"inspect", "show"},
            {"<file>"},
            {
                {"--json",                 "Emit machine-readable JSON output"},
                {"--verify",               "Also verify block hashes"},
            },
            false,
        },
        {
            "verify", "Verify SHA1 integrity of an STFS file",
            {"check"},
            {"<file>"},
            {
                {"--json",                 "Emit machine-readable JSON output"},
            },
            false,
        },
        {
            "disable", "Disable an installed package (rename to .disabled)",
            {},
            {"<title_id> <content_id_prefix>"},
            {
                {"--content-root <path>",  "Override the content root directory"},
            },
            false,
        },
        {
            "enable", "Re-enable a previously-disabled package",
            {},
            {"<title_id> <content_id_prefix>"},
            {
                {"--content-root <path>",  "Override the content root directory"},
            },
            false,
        },
        {
            "search", "Search installed packages by title ID or name",
            {"find"},
            {"<query>"},
            {
                {"--content-root <path>",  "Override the content root directory"},
                {"--json",                 "Emit machine-readable JSON output"},
            },
            false,
        },
        {
            "saves", "List all save files (content_type=00000001)",
            {},
            {"[title_id]"},
            {
                {"--content-root <path>",  "Override the content root directory"},
                {"--json",                 "Emit machine-readable JSON output"},
            },
            false,
        },
        {
            "export-save", "Export a save file to a directory",
            {},
            {"<title_id> <file_name> <output_dir>"},
            {
                {"--content-root <path>",  "Override the content root directory"},
            },
            false,
        },
    };
    return specs;
}

constexpr const char* VERSION = "xbox-content-installer 1.0.0";
constexpr const char* BANNER =
    "xbox-content-installer 1.0.0 - Cross-platform Xbox 360 TU/DLC installer\n"
    "Compatible with Xenia-canary content layout\n"
    "\n"
    "USAGE:\n"
    "  xbox-install <command> [options] [args...]\n"
    "\n"
    "COMMANDS:\n";
} // namespace

const std::vector<CommandSpec>& command_specs() { return specs_table(); }

std::string format_help(std::string_view command) {
    std::string out;
    if (command.empty()) {
        out.append(BANNER);
        for (const auto& s : specs_table()) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "  %-12s %s\n", s.name.c_str(), s.description.c_str());
            out.append(buf);
        }
        out.append("\nGLOBAL OPTIONS:\n");
        out.append("  --help, -h     Show this help\n");
        out.append("  --version, -V  Show version\n");
        out.append("  --verbose, -v  Increase verbosity\n");
        out.append("  --json         Emit machine-readable JSON\n");
        out.append("\nRun 'xbox-install <command> --help' for command-specific help.\n");
    } else {
        for (const auto& s : specs_table()) {
            if (s.name == command) {
                out.append("xbox-install ").append(s.name).append(" - ").append(s.description).append("\n\n");
                out.append("USAGE:\n  xbox-install ").append(s.name).append(" [options] ");
                for (const auto& p : s.positional_args) {
                    out.append(p).append(" ");
                }
                if (s.accepts_variadic_positionals) out.append("...");
                out.append("\n\nOPTIONS:\n");
                for (const auto& [flag, desc] : s.flags) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "  %-30s %s\n", flag.c_str(), desc.c_str());
                    out.append(buf);
                }
                if (!s.aliases.empty()) {
                    out.append("\nALIASES: ");
                    for (std::size_t i = 0; i < s.aliases.size(); ++i) {
                        if (i) out.append(", ");
                        out.append(s.aliases[i]);
                    }
                    out.append("\n");
                }
                return out;
            }
        }
        out.append("Unknown command: ").append(command).append("\n");
        out.append(format_help(""));
    }
    return out;
}

std::string format_version() {
    return std::string(VERSION) + "\n";
}

namespace {
void apply_global_flag(ParsedArgs& args, const std::string& name, const std::string& value) {
    if (name == "help" || name == "h") {
        args.help_requested = true;
    } else if (name == "version" || name == "V") {
        args.version_requested = true;
    } else if (name == "verbose" || name == "v") {
        args.verbose++;
    } else if (name == "json") {
        args.json_output = true;
    } else if (name == "dry-run") {
        args.dry_run = true;
    } else if (name == "threads") {
        auto n = str::parse_int<std::size_t>(value);
        if (n) args.threads = *n;
    } else if (name == "content-root") {
        args.content_root = value;
    } else if (name == "on-conflict") {
        args.conflict_policy = value;
    } else if (name == "xuid") {
        args.xuid_override = value;
    } else if (name == "no-verify") {
        args.flags["no-verify"] = "true";
    } else if (name == "no-mmap") {
        args.no_mmap = true;
    } else if (name == "allow-unknown") {
        args.flags["allow-unknown"] = "true";
    } else if (name == "disabled") {
        args.flags["disabled"] = "true";
    } else if (name == "verify") {
        args.flags["verify"] = "true";
    } else {
        // Store unknown flags for the command to read
        args.flags[name] = value;
    }
}
} // namespace

Result<ParsedArgs, Error> parse_args(int argc, char** argv) {
    ParsedArgs args;

    if (argc < 2) {
        args.help_requested = true;
        return args;
    }

    // Skip argv[0] (program name)
    int i = 1;

    // First, check for global help/version (no command yet)
    std::string first = argv[i];
    if (first == "--help" || first == "-h") {
        args.help_requested = true;
        return args;
    }
    if (first == "--version" || first == "-V") {
        args.version_requested = true;
        return args;
    }

    args.command = first;
    ++i;

    // Parse remaining args
    while (i < argc) {
        std::string arg = argv[i];

        if (arg.starts_with("--")) {
            // Long flag: --name or --name=value
            std::string name = arg.substr(2);
            std::string value;
            if (auto eq = name.find('='); eq != std::string::npos) {
                value = name.substr(eq + 1);
                name = name.substr(0, eq);
            } else {
                // Peek at next arg for value if it's not a flag
                if (i + 1 < argc && argv[i+1][0] != '-') {
                    value = argv[i+1];
                    ++i;
                } else {
                    value = "true";
                }
            }
            apply_global_flag(args, name, value);
        } else if (arg.starts_with("-") && arg.size() > 1) {
            // Short flag(s): -v, -vv, -jN, -j N
            for (std::size_t j = 1; j < arg.size(); ++j) {
                char c = arg[j];
                if (c == 'v') {
                    args.verbose++;
                } else if (c == 'h') {
                    args.help_requested = true;
                } else if (c == 'V') {
                    args.version_requested = true;
                } else if (c == 'j') {
                    args.json_output = true;
                } else {
                    // Unknown short flag - store the rest as a flag
                    args.flags[std::string(1, c)] = "true";
                }
            }
        } else {
            // Positional argument
            args.positionals.push_back(arg);
        }
        ++i;
    }

    return args;
}

} // namespace xbox::cli
