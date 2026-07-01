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
#include "xbox/cli/commands.hpp"

#include "xbox/cli/cli_parser.hpp"
#include "xbox/cli/progress.hpp"
#include "xbox/core/logger.hpp"
#include "xbox/installer/installer.hpp"
#include "xbox/installer/path_resolver.hpp"
#include "xbox/io/file_io.hpp"
#include "xbox/io/memory_map.hpp"
#include "xbox/io/zip_reader.hpp"
#include "xbox/stfs/stfs_extractor.hpp"
#include "xbox/stfs/stfs_metadata.hpp"
#include "xbox/stfs/stfs_reader.hpp"
#include "xbox/utils/path_utils.hpp"
#include "xbox/utils/string_utils.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace xbox::cli {

namespace fs = std::filesystem;

namespace {

// Configure the logger from ParsedArgs
void configure_logger(const ParsedArgs& args) {
    using namespace xbox::log;
    Level l = Level::Info;
    if (args.verbose >= 2) l = Level::Trace;
    else if (args.verbose >= 1) l = Level::Debug;
    logger().set_level(l);
    // Disable colors in JSON mode (so logs are easy to parse)
    if (args.json_output) logger().set_color_enabled(false);
}

// Get the config file path for saving/loading the default content root
// Uses XDG config home on Linux, APPDATA on Windows, ~/Library on macOS
fs::path get_config_file_path() {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    if (appdata) return fs::path(appdata) / "xbox-install" / "config.txt";
    return fs::path("xbox-install") / "config.txt";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home) return fs::path(home) / "Library" / "Application Support" / "xbox-install" / "config.txt";
    return fs::path("xbox-install") / "config.txt";
#else
    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    if (xdg_config) return fs::path(xdg_config) / "xbox-install" / "config.txt";
    const char* home = std::getenv("HOME");
    if (home) return fs::path(home) / ".config" / "xbox-install" / "config.txt";
    return fs::path("xbox-install") / "config.txt";
#endif
}

// Save the content root to config file so it's remembered for future invocations
void save_content_root(const std::string& root) {
    namespace fs = std::filesystem;
    try {
        auto config_path = get_config_file_path();
        std::error_code ec;
        fs::create_directories(config_path.parent_path(), ec);
        std::ofstream f(config_path);
        if (f) {
            f << root;
        }
    } catch (...) {
        // best-effort
    }
}

// Load the saved content root, or return empty if not set
std::string load_content_root() {
    namespace fs = std::filesystem;
    try {
        auto config_path = get_config_file_path();
        if (!fs::exists(config_path)) return "";
        std::ifstream f(config_path);
        std::string root;
        if (f) {
            std::getline(f, root);
            // trim whitespace
            while (!root.empty() && (root.back() == '\r' || root.back() == '\n' || root.back() == ' ')) {
                root.pop_back();
            }
        }
        return root;
    } catch (...) {
        return "";
    }
}

// Resolve the content root: use --content-root if provided, else saved config, else "content"
std::string resolve_content_root(const ParsedArgs& args) {
    if (!args.content_root.empty()) {
        // User provided --content-root, save it for future use
        save_content_root(args.content_root);
        return args.content_root;
    }
    // Try loading from config
    std::string saved = load_content_root();
    if (!saved.empty()) {
        return saved;
    }
    // Default fallback
    return "content";
}

// Configure the path resolver from ParsedArgs
xbox::installer::PathResolver make_resolver(const ParsedArgs& args) {
    std::string root = resolve_content_root(args);
    xbox::installer::PathResolver resolver(xbox::path::expand_home(root));

    // Apply --xuid override if provided
    if (!args.xuid_override.empty()) {
        // Parse hex string to u64
        auto xuid = str::parse_hex_u64(args.xuid_override);
        if (xuid) {
            resolver.set_xuid_override(*xuid);
            XBOX_LOG_INFO("Using XUID override: {:016X}", *xuid);
        } else {
            XBOX_LOG_WARN("Invalid XUID format: {} (expected 16-char hex)", args.xuid_override);
        }
    }

    return resolver;
}

// Configure install options from ParsedArgs
xbox::installer::InstallOptions make_install_options(const ParsedArgs& args) {
    xbox::installer::InstallOptions opts;
    opts.verify = !args.flags.contains("no-verify");
    opts.dry_run = args.dry_run;
    opts.no_mmap = args.no_mmap;
    opts.workers_per_package = args.threads;
    opts.parallel_packages = std::min<std::size_t>(2u, std::thread::hardware_concurrency());
    if (args.conflict_policy == "overwrite") opts.conflict = xbox::installer::ConflictPolicy::Overwrite;
    else if (args.conflict_policy == "rename") opts.conflict = xbox::installer::ConflictPolicy::Rename;
    else if (args.conflict_policy == "fail")   opts.conflict = xbox::installer::ConflictPolicy::Fail;
    else opts.conflict = xbox::installer::ConflictPolicy::Skip;
    opts.allow_unknown_content_type = args.flags.contains("allow-unknown");
    return opts;
}

// Read the display_name from a .header file (Xenia metadata)
// Returns empty string if file doesn't exist or can't be read
std::string read_header_display_name(const fs::path& header_file) {
    if (!fs::exists(header_file)) return "";
    std::ifstream f(header_file, std::ios::binary);
    if (!f) return "";
    // display_name is at offset 0x008, 256 bytes (128 UTF-16BE chars)
    f.seekg(0x008);
    if (!f) return "";
    std::vector<char> raw(256, 0);
    f.read(raw.data(), 256);
    if (!f) return "";
    // Convert UTF-16BE to UTF-8 (simple BMP conversion)
    std::string result;
    for (int i = 0; i < 256; i += 2) {
        u16 cp = (static_cast<u16>(static_cast<unsigned char>(raw[i])) << 8) |
                  static_cast<u16>(static_cast<unsigned char>(raw[i+1]));
        if (cp == 0) break;  // null terminator
        if (cp < 0x80) {
            result.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return result;
}

// Get content type name in Arabic/English for display
std::string content_type_display_name(u32 content_type) {
    switch (content_type) {
        case 0x000B0000: return "Title Update (TU)";
        case 0x00000002: return "DLC / Marketplace";
        case 0x00000001: return "Saved Game";
        case 0x00004000: return "Installed Game";
        case 0x00007000: return "Xbox 360 Title";
        case 0x000D0000: return "Arcade Title";
        case 0x00080000: return "Game Demo";
        case 0x00010000: return "Profile";
        case 0x00020000: return "Gamer Picture";
        case 0x00030000: return "Theme";
        default: return "Unknown (0x" + str::format_hex_u32(content_type) + ")";
    }
}

// Compute the .header file path for an InstalledEntry
fs::path compute_header_path(const installer::PathResolver::InstalledEntry& e) {
    // entry.path = .../<title_id>/<content_type>/<file_name>
    // header = .../<title_id>/Headers/<content_type>/<file_name>.header
    return e.path.parent_path().parent_path() / "Headers" /
           str::format_hex_u32(e.content_type) /
           (e.file_name + ".header");
}

// Parse --xuid from args, returns nullopt if not provided
std::optional<u64> parse_xuid_filter(const ParsedArgs& args) {
    if (args.xuid_override.empty()) return std::nullopt;
    auto xuid = str::parse_hex_u64(args.xuid_override);
    if (xuid) {
        return xuid;
    }
    return std::nullopt;
}

// Check if an entry matches the XUID filter (if set)
// TU and DLC (content_type 0x000B0000 and 0x00000002) are shared across ALL profiles
// (stored under xuid=0000000000000000), so they should ALWAYS be shown.
// Saves (0x00000001) and Profiles (0x00010000) are per-profile.
bool xuid_matches(const installer::PathResolver::InstalledEntry& e,
                  std::optional<u64> xuid_filter) {
    if (!xuid_filter) return true;  // no filter = match all

    // TU and DLC are shared content — always show them regardless of --xuid
    if (e.content_type == 0x000B0000 ||  // Title Update
        e.content_type == 0x00000002 ||  // DLC / Marketplace
        e.content_type == 0x00007000 ||  // Xbox 360 Title
        e.content_type == 0x000D0000 ||  // Arcade Title
        e.content_type == 0x00004000 ||  // Installed Game
        e.content_type == 0x00080000) {  // Game Demo
        return true;
    }

    // For saves, profiles, and other per-profile content, filter by xuid
    return e.xuid == *xuid_filter;
}

// Format XUID for display
std::string format_xuid_short(u64 xuid) {
    return installer::PathResolver::format_xuid(xuid);
}

// Pretty-print JSON: takes a compact JSON string and adds indentation
std::string json_pretty_print(const std::string& compact_json) {
    std::string out;
    out.reserve(compact_json.size() * 2);
    int indent = 0;
    bool in_string = false;

    for (std::size_t i = 0; i < compact_json.size(); ++i) {
        char c = compact_json[i];

        if (c == '"' && (i == 0 || compact_json[i-1] != '\\')) {
            in_string = !in_string;
            out.push_back(c);
            continue;
        }

        if (in_string) {
            out.push_back(c);
            continue;
        }

        switch (c) {
            case '{':
            case '[':
                out.push_back(c);
                if (i + 1 < compact_json.size() && compact_json[i+1] != '}' && compact_json[i+1] != ']') {
                    ++indent;
                    out.push_back('\n');
                    for (int j = 0; j < indent * 2; ++j) out.push_back(' ');
                }
                break;
            case '}':
            case ']':
                if (i > 0 && compact_json[i-1] != '{' && compact_json[i-1] != '[') {
                    --indent;
                    out.push_back('\n');
                    for (int j = 0; j < indent * 2; ++j) out.push_back(' ');
                } else {
                    --indent;
                }
                out.push_back(c);
                break;
            case ',':
                out.push_back(c);
                out.push_back('\n');
                for (int j = 0; j < indent * 2; ++j) out.push_back(' ');
                break;
            case ':':
                out.push_back(c);
                out.push_back(' ');
                break;
            default:
                out.push_back(c);
        }
    }
    return out;
}

// Emit JSON output (pretty-printed if not --json-compact)
void emit_json(const ParsedArgs& args, const std::string& json_str) {
    if (args.flags.contains("compact")) {
        std::cout << json_str << "\n";
    } else {
        std::cout << json_pretty_print(json_str) << "\n";
    }
}

// Escape a string for JSON output
std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\b': out.append("\\b"); break;
            case '\f': out.append("\\f"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                if (static_cast<unsigned char>(c) < 32) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned int>(static_cast<unsigned char>(c)));
                    out.append(buf);
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
    return out;
}

} // namespace

void emit_result(const ParsedArgs& args, bool success,
                 std::string_view message,
                 const std::string& json_payload) {
    if (args.json_output) {
        if (!json_payload.empty()) {
            std::cout << json_payload << "\n";
        } else {
            std::cout << "{\"ok\":" << (success ? "true" : "false")
                      << ",\"message\":" << json_escape(message) << "}\n";
        }
    } else {
        if (success) {
            if (!message.empty()) std::cout << message << "\n";
        } else {
            std::cerr << "ERROR: " << message << "\n";
        }
    }
}

// ---------------------------------------------------------------------------
// install command
// ---------------------------------------------------------------------------
int cmd_install(const ParsedArgs& args) {
    using namespace xbox;

    if (args.positionals.empty()) {
        emit_result(args, false, "install: no input files specified");
        return exit_code::USAGE;
    }

    auto resolver = make_resolver(args);
    auto opts = make_install_options(args);

    // Resolve file paths (expand ~, normalize)
    // Handle ZIP files separately - extract them first
    std::vector<fs::path> files;
    std::vector<fs::path> temp_dirs;  // track temp dirs for cleanup

    for (const auto& f : args.positionals) {
        fs::path file_path = path::expand_home(f);

        // Check if it's a ZIP file (Xenia Manager export or zipped STFS)
        if (str::ends_with(str::to_lower(file_path.string()), ".zip")) {
            XBOX_LOG_INFO("Detected ZIP file: {}", file_path.string());

            auto zip_r = io::ZipReader::open(file_path);
            if (!zip_r.is_ok()) {
                emit_result(args, false, "Failed to open ZIP: " + zip_r.error().message());
                return exit_code::NOINPUT;
            }

            auto entries = zip_r.value().list_entries();
            XBOX_LOG_INFO("ZIP contains {} entries", entries.size());

            // Smart detection: what type of ZIP is this?
            // Type 1: Xenia Manager export (has <title_id>/00000001/<save>/ structure + .header files)
            // Type 2: Zipped STFS container (contains a single CON/LIVE/PIRS file)
            // Type 3: Zipped extracted folder (has 00000001/ but no .header)

            bool has_header_file = false;
            bool has_stfs_file = false;
            std::string stfs_entry_name;
            int non_dir_count = 0;

            for (const auto& e : entries) {
                if (!e.is_directory) {
                    ++non_dir_count;
                    if (str::ends_with(e.name, ".header")) {
                        has_header_file = true;
                    }
                    // Check if any file starts with CON/LIVE/PIRS magic
                    if (non_dir_count <= 3 && e.uncompressed_size >= 4) {
                        auto data_r = zip_r.value().read_file(e.name);
                        if (data_r.is_ok() && data_r.value().size() >= 4) {
                            auto& d = data_r.value();
                            if ((d[0] == 'C' && d[1] == 'O' && d[2] == 'N' && d[3] == ' ') ||
                                (d[0] == 'L' && d[1] == 'I' && d[2] == 'V' && d[3] == 'E') ||
                                (d[0] == 'P' && d[1] == 'I' && d[2] == 'R' && d[3] == 'S')) {
                                has_stfs_file = true;
                                stfs_entry_name = e.name;
                            }
                        }
                    }
                }
            }

            if (has_stfs_file && !has_header_file) {
                // Type 2: Zipped STFS container — extract the CON file to a temp location
                // and install it as a regular STFS package
                XBOX_LOG_INFO("ZIP contains STFS container: {}", stfs_entry_name);

                // Extract the STFS file to a temp file
                auto temp_dir = fs::temp_directory_path() / "xbox_install_zip";
                fs::create_directories(temp_dir);
                auto temp_file = temp_dir / fs::path(stfs_entry_name).filename();

                auto extract_r = zip_r.value().extract_file(stfs_entry_name, temp_file);
                if (!extract_r.is_ok()) {
                    emit_result(args, false, "Failed to extract STFS from ZIP: " + extract_r.error().message());
                    return exit_code::IOERR;
                }

                XBOX_LOG_INFO("Extracted STFS to temp: {}", temp_file.string());
                files.push_back(temp_file);
                continue;
            }

            // Type 1 or 3: Xenia Manager export or extracted folder
            // Read the .header file (or infer from structure) to get xuid and title_id
            u64 xuid = 0;
            u32 title_id = 0;
            u32 content_type = 0;

            // Try to read from .header file first
            std::string header_entry_name;
            for (const auto& e : entries) {
                if (str::ends_with(e.name, ".header") && !e.is_directory) {
                    header_entry_name = e.name;
                    break;
                }
            }

            if (!header_entry_name.empty()) {
                auto header_data_r = zip_r.value().read_file(header_entry_name);
                if (header_data_r.is_ok() && header_data_r.value().size() >= 0x148) {
                    auto& hd = header_data_r.value();
                    content_type = (static_cast<u32>(hd[4]) << 24) | (static_cast<u32>(hd[5]) << 16) |
                                   (static_cast<u32>(hd[6]) << 8) | static_cast<u32>(hd[7]);
                    xuid = 0;
                    for (int i = 0; i < 8; ++i) {
                        xuid = (xuid << 8) | hd[0x134 + i];
                    }
                    title_id = (static_cast<u32>(hd[0x140]) << 24) | (static_cast<u32>(hd[0x141]) << 16) |
                               (static_cast<u32>(hd[0x142]) << 8) | static_cast<u32>(hd[0x143]);
                    XBOX_LOG_INFO("ZIP save: title_id={:08X}, xuid={:016X}, content_type={:08X}",
                                  title_id, xuid, content_type);
                }
            }

            // If no header found, try to infer from folder structure
            if (title_id == 0) {
                // Look for a folder name that looks like a title_id (8 hex chars)
                for (const auto& e : entries) {
                    if (e.is_directory) {
                        auto name = e.name;
                        // Remove trailing /
                        if (!name.empty() && name.back() == '/') name.pop_back();
                        // Get last component
                        auto pos = name.find_last_of('/');
                        if (pos != std::string::npos) name = name.substr(pos + 1);
                        // Check if it's 8 hex chars
                        if (name.size() == 8) {
                            auto tid = str::parse_hex_u32(name);
                            if (tid) {
                                title_id = *tid;
                                XBOX_LOG_INFO("Inferred title_id from folder: {:08X}", title_id);
                                break;
                            }
                        }
                    }
                }
            }

            // Determine the xuid directory
            std::string xuid_str;
            if (!args.xuid_override.empty()) {
                xuid_str = args.xuid_override;
                while (xuid_str.size() < 16) xuid_str = "0" + xuid_str;
                xuid_str = str::to_upper(xuid_str);
                XBOX_LOG_INFO("Using --xuid override for ZIP: {}", xuid_str);
            } else if (content_type == 0x00000002 || content_type == 0x000B0000) {
                xuid_str = "0000000000000000";
            } else if (xuid != 0) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%016llX",
                              static_cast<unsigned long long>(xuid));
                xuid_str = buf;
            } else {
                // Default to 0000000000000000 if we can't determine
                xuid_str = "0000000000000000";
            }

            // Extract to content_root/<xuid>/
            auto root = resolver.root();
            auto extract_dest = root / xuid_str;

            XBOX_LOG_INFO("Extracting ZIP to: {}", extract_dest.string());
            auto extract_r = zip_r.value().extract_all(extract_dest);
            if (!extract_r.is_ok()) {
                emit_result(args, false, "Failed to extract ZIP: " + extract_r.error().message());
                return exit_code::IOERR;
            }

            XBOX_LOG_INFO("ZIP extracted successfully to profile {}", xuid_str);
            continue;
        }

        files.push_back(file_path);
    }

    // If all files were ZIPs, we're done
    if (files.empty()) {
        emit_result(args, true, "All ZIP files extracted successfully");
        return exit_code::OK;
    }

    // Progress reporting - use shared_ptr so it outlives the install_packages call
    std::shared_ptr<cli::ProgressReporter> progress;
    if (!args.json_output) {
        std::size_t total_files = 0;
        for (const auto& f : files) {
            try {
                if (!io::file_exists(f)) {
                    XBOX_LOG_WARN("File not found: {}", f.string());
                    continue;
                }
                // Disable progress bar temporarily during pre-scan to avoid UI spam
                // (the StfsReader::open will trigger mmap debug logs)
                auto r = stfs::StfsReader::open(f, args.no_mmap);
                if (r.is_ok()) {
                    // Count only actual files, not directories
                    for (const auto& entry : r.value().file_entries()) {
                        if (!entry.is_directory()) {
                            ++total_files;
                        }
                    }
                } else {
                    XBOX_LOG_WARN("Could not pre-scan {}: {}", f.string(), r.error().message());
                }
            } catch (const std::exception& e) {
                XBOX_LOG_WARN("Error scanning {}: {}", f.string(), e.what());
            } catch (...) {
                // best effort
            }
        }
        if (total_files > 0) {
            progress = std::make_shared<cli::ProgressReporter>(total_files, "files");
            progress->set_current_label("Installing");
            opts.on_progress = [progress](std::size_t cur, std::size_t total,
                                           std::string_view path,
                                           std::string_view pkg) {
                // Clear the progress bar line before printing logs to avoid overlap
                std::fprintf(stderr, "\r\033[K");  // clear current line
                std::fflush(stderr);
                progress->set_current_label(path);
                progress->set_current(cur);
            };
        }
    }

    auto report = installer::install_packages(files, resolver, opts);
    if (!report.is_ok()) {
        emit_result(args, false, report.error().message());
        return exit_code::DATAERR;
    }

    auto& r = report.value();
    if (args.json_output) {
        std::ostringstream oss;
        oss << "{\"ok\":true,\"succeeded\":" << r.succeeded
            << ",\"failed\":" << r.failed
            << ",\"skipped\":" << r.skipped
            << ",\"bytes_written\":" << r.total_bytes_written
            << ",\"packages\":[";
        for (std::size_t i = 0; i < r.packages.size(); ++i) {
            const auto& p = r.packages[i];
            if (i) oss << ",";
            oss << "{\"source\":" << json_escape(p.source_path.string())
                << ",\"destination\":" << json_escape(p.location.content_id_dir.string())
                << ",\"skipped\":" << (p.skipped ? "true" : "false")
                << ",\"error\":" << json_escape(p.error)
                << ",\"files_extracted\":" << p.extraction.files_extracted
                << ",\"files_skipped\":" << p.extraction.files_skipped
                << ",\"bytes_written\":" << p.extraction.total_bytes_written
                << "}";
        }
        oss << "]}";
        emit_json(args, oss.str());
    } else {
        for (const auto& p : r.packages) {
            std::cout << p.format_summary() << "\n";
        }
        std::cout << r.format_summary() << "\n";
    }

    if (r.failed > 0) return exit_code::DATAERR;
    if (r.succeeded == 0 && r.skipped > 0) return exit_code::OK;
    return exit_code::OK;
}

// ---------------------------------------------------------------------------
// uninstall command
// ---------------------------------------------------------------------------
int cmd_uninstall(const ParsedArgs& args) {
    using namespace xbox;
    if (args.positionals.size() < 2) {
        emit_result(args, false, "uninstall: requires <title_id> <file_name>");
        return exit_code::USAGE;
    }
    auto tid = str::parse_hex_u32(args.positionals[0]);
    if (!tid) {
        emit_result(args, false, "invalid title ID: " + args.positionals[0]);
        return exit_code::USAGE;
    }
    auto resolver = make_resolver(args);
    auto xuid_filter = parse_xuid_filter(args);
    auto r = installer::uninstall_package(*tid, args.positionals[1], resolver, xuid_filter);
    if (!r.is_ok()) {
        emit_result(args, false, r.error().message());
        return exit_code::DATAERR;
    }
    std::string msg = "Uninstalled " + args.positionals[0] + "/" + args.positionals[1];
    if (xuid_filter) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%016llX",
                      static_cast<unsigned long long>(*xuid_filter));
        msg += " (profile: " + std::string(buf) + ")";
    }
    emit_result(args, true, msg);
    return exit_code::OK;
}

// ---------------------------------------------------------------------------
// list command
// ---------------------------------------------------------------------------
int cmd_list(const ParsedArgs& args) {
    using namespace xbox;
    auto resolver = make_resolver(args);
    auto xuid_filter = parse_xuid_filter(args);

    std::string filter_desc;
    if (xuid_filter) {
        filter_desc = " (Profile: " + format_xuid_short(*xuid_filter) + ")";
    }

    if (args.positionals.empty()) {
        // List all titles with full summary
        auto titles = installer::PathResolver(resolver).list_installed_titles();
        if (!titles.is_ok()) {
            emit_result(args, false, titles.error().message());
            return exit_code::IOERR;
        }
        if (args.json_output) {
            std::ostringstream oss;
            oss << "{\"titles\":[";
            for (std::size_t i = 0; i < titles.value().size(); ++i) {
                if (i) oss << ",";
                oss << "\"" << str::format_hex_u32(titles.value()[i]) << "\"";
            }
            oss << "]}";
            emit_json(args, oss.str());
        } else {
            if (titles.value().empty()) {
                std::cout << "No installed titles" << filter_desc << ".\n";
            } else {
                std::cout << "═══════════════════════════════════════════════════\n";
                std::cout << "  Installed Content" << filter_desc << "\n";
                std::cout << "═══════════════════════════════════════════════════\n\n";

                int total_tu = 0, total_dlc = 0, total_saves = 0;

                for (auto t : titles.value()) {
                    // Get all entries for this title
                    auto entries_r = resolver.list_installed_for_title(t);
                    if (!entries_r.is_ok()) continue;

                    // Filter by xuid
                    std::vector<const installer::PathResolver::InstalledEntry*> filtered;
                    std::string game_name;
                    int tu_count = 0, dlc_count = 0, save_count = 0;

                    for (const auto& e : entries_r.value()) {
                        if (!xuid_matches(e, xuid_filter)) continue;
                        if (e.is_disabled && !args.flags.contains("disabled")) continue;
                        filtered.push_back(&e);

                        // Count by type
                        if (e.content_type == 0x000B0000) ++tu_count;
                        else if (e.content_type == 0x00000002) ++dlc_count;
                        else if (e.content_type == 0x00000001) ++save_count;

                        // Get game name from first available header
                        if (game_name.empty()) {
                            auto header_path = compute_header_path(e);
                            game_name = read_header_display_name(header_path);
                        }
                    }

                    if (filtered.empty()) continue;

                    total_tu += tu_count;
                    total_dlc += dlc_count;
                    total_saves += save_count;

                    // Print title header
                    std::cout << "  " << str::format_hex_u32(t);
                    if (!game_name.empty()) {
                        std::cout << "  " << game_name;
                    }
                    std::cout << "\n";

                    // Print summary counts
                    std::cout << "    ";
                    bool first_type = true;
                    if (tu_count > 0) {
                        std::cout << "TU: " << tu_count;
                        first_type = false;
                    }
                    if (dlc_count > 0) {
                        if (!first_type) std::cout << "  |  ";
                        std::cout << "DLC: " << dlc_count;
                        first_type = false;
                    }
                    if (save_count > 0) {
                        if (!first_type) std::cout << "  |  ";
                        std::cout << "Saves: " << save_count;
                        first_type = false;
                    }
                    std::cout << "\n";

                    // Print each package
                    for (const auto* e : filtered) {
                        auto header_path = compute_header_path(*e);
                        auto display_name = read_header_display_name(header_path);

                        std::string type_icon;
                        if (e->content_type == 0x000B0000) type_icon = "🔧";
                        else if (e->content_type == 0x00000002) type_icon = "📦";
                        else if (e->content_type == 0x00000001) type_icon = "📁";
                        else type_icon = "📄";

                        std::cout << "    " << type_icon << " ";
                        if (!display_name.empty()) {
                            std::cout << display_name;
                        } else {
                            std::cout << e->file_name;
                        }
                        if (e->is_disabled) std::cout << " (DISABLED)";
                        std::cout << "\n";
                    }
                    std::cout << "\n";
                }

                // Print grand total
                std::cout << "─────────────────────────────────────────────\n";
                std::cout << "  Total: " << total_tu << " TU  |  "
                          << total_dlc << " DLC  |  "
                          << total_saves << " Saves\n";
                std::cout << "─────────────────────────────────────────────\n";
            }
        }
        return exit_code::OK;
    }

    // List packages for a specific title
    auto tid = str::parse_hex_u32(args.positionals[0]);
    if (!tid) {
        emit_result(args, false, "invalid title ID: " + args.positionals[0]);
        return exit_code::USAGE;
    }
    auto entries = resolver.list_installed_for_title(*tid);
    if (!entries.is_ok()) {
        emit_result(args, false, entries.error().message());
        return exit_code::IOERR;
    }
    bool include_disabled = args.flags.contains("disabled");

    // Filter entries
    std::vector<const installer::PathResolver::InstalledEntry*> filtered;
    for (const auto& e : entries.value()) {
        if (e.is_disabled && !include_disabled) continue;
        if (!xuid_matches(e, xuid_filter)) continue;
        filtered.push_back(&e);
    }

    if (args.json_output) {
        std::ostringstream oss;
        oss << "{\"title_id\":\"" << str::format_hex_u32(*tid) << "\",\"packages\":[";
        bool first = true;
        for (const auto* e : filtered) {
            if (!first) oss << ",";
            first = false;
            auto header_path = compute_header_path(*e);
            auto display_name = read_header_display_name(header_path);
            oss << "{\"content_type\":\"" << str::format_hex_u32(e->content_type) << "\""
                << ",\"content_type_name\":" << json_escape(content_type_display_name(e->content_type))
                << ",\"file_name\":" << json_escape(e->file_name)
                << ",\"display_name\":" << json_escape(display_name)
                << ",\"xuid\":\"" << format_xuid_short(e->xuid) << "\""
                << ",\"path\":" << json_escape(e->path.string())
                << ",\"disabled\":" << (e->is_disabled ? "true" : "false")
                << "}";
        }
        oss << "]}";
        emit_json(args, oss.str());
    } else {
        if (filtered.empty()) {
            std::cout << "No packages installed for title " << str::format_hex_u32(*tid)
                      << filter_desc << "\n";
        } else {
            std::cout << "Packages for title " << str::format_hex_u32(*tid)
                      << filter_desc << ":\n";
            std::cout << "═════════════════════════════════════════════\n";
            for (const auto* e : filtered) {
                auto header_path = compute_header_path(*e);
                auto display_name = read_header_display_name(header_path);

                std::string type_icon;
                if (e->content_type == 0x000B0000) type_icon = "🔧 TU";
                else if (e->content_type == 0x00000002) type_icon = "📦 DLC";
                else if (e->content_type == 0x00000001) type_icon = "📁 Save";
                else type_icon = "📄 Other";

                std::cout << "  [" << type_icon << "] ";
                if (!display_name.empty()) {
                    std::cout << display_name;
                } else {
                    std::cout << e->file_name;
                }
                if (e->is_disabled) std::cout << "  (DISABLED)";
                std::cout << "\n";
                std::cout << "    ID: " << e->file_name << "\n";
                std::cout << "    Profile: " << format_xuid_short(e->xuid) << "\n";
                std::cout << "    Path: " << e->path.string() << "\n\n";
            }
        }
    }
    return exit_code::OK;
}

// ---------------------------------------------------------------------------
// info command
// ---------------------------------------------------------------------------
int cmd_info(const ParsedArgs& args) {
    using namespace xbox;
    if (args.positionals.empty()) {
        emit_result(args, false, "info: requires <file>");
        return exit_code::USAGE;
    }
    auto file_path = path::expand_home(args.positionals[0]);
    if (!io::file_exists(file_path)) {
        emit_result(args, false, "file not found: " + file_path.string());
        return exit_code::NOINPUT;
    }

    auto reader = stfs::StfsReader::open(file_path, args.no_mmap);
    if (!reader.is_ok()) {
        emit_result(args, false, reader.error().message());
        return exit_code::DATAERR;
    }

    const auto& h = reader.value().header();
    auto resolver = make_resolver(args);
    auto loc_r = resolver.resolve(h, file_path.filename().string());
    if (!loc_r.is_ok()) {
        emit_result(args, false, loc_r.error().message());
        return exit_code::DATAERR;
    }
    const auto& loc = loc_r.value();

    if (args.json_output) {
        std::ostringstream oss;
        oss << "{\"ok\":true"
            << ",\"file\":" << json_escape(file_path.string())
            << ",\"package_type\":\"" << stfs::package_type_name(h.type) << "\""
            << ",\"volume_type\":\"" << (h.is_svod() ? "SVOD" : "STFS") << "\""
            << ",\"title_id\":\"" << str::format_hex_u32(h.title_id) << "\""
            << ",\"content_type\":\"" << str::format_hex_u32(h.content_type) << "\""
            << ",\"content_type_name\":" << json_escape(h.content_type_name())
            << ",\"content_id\":\"" << str::to_hex(h.content_id.data(), h.content_id.size()) << "\""
            << ",\"display_name\":" << json_escape(h.display_name)
            << ",\"description\":" << json_escape(h.display_description)
            << ",\"publisher\":" << json_escape(h.publisher_name)
            << ",\"title_name\":" << json_escape(h.title_name)
            << ",\"version\":" << h.version
            << ",\"base_version\":" << h.base_version
            << ",\"media_id\":\"" << str::format_hex_u32(h.media_id) << "\""
            << ",\"content_size\":" << h.content_size
            << ",\"metadata_version\":" << h.metadata_version
            << ",\"license_mask\":\"" << str::format_hex_u32(h.license_mask()) << "\""
            << ",\"profile_id\":\"" << str::to_hex(h.profile_id.data(), h.profile_id.size()) << "\""
            << ",\"file_count\":" << reader.value().file_entries().size()
            << ",\"install_path\":" << json_escape(loc.content_id_dir.string())
            << "}";
        emit_json(args, oss.str());
    } else {
        std::cout << "=== STFS Package Info ===\n";
        std::cout << h.format_summary();
        std::cout << "\nFile count: " << reader.value().file_entries().size() << "\n";
        std::cout << "\nInstall path would be:\n  " << loc.content_id_dir.string() << "\n";

        if (args.flags.contains("verify")) {
            std::cout << "\nVerifying block hashes...\n";
            auto report = reader.value().verify_all_blocks();
            if (!report.is_ok()) {
                std::cout << "Verification error: " << report.error().message() << "\n";
                return exit_code::DATAERR;
            }
            std::cout << "  Total blocks: " << report.value().total_blocks << "\n";
            std::cout << "  OK:           " << report.value().verified_ok << "\n";
            std::cout << "  Failed:       " << report.value().failed << "\n";
            if (report.value().failed > 0) return exit_code::DATAERR;
        }
    }
    return exit_code::OK;
}

// ---------------------------------------------------------------------------
// verify command
// ---------------------------------------------------------------------------
int cmd_verify(const ParsedArgs& args) {
    using namespace xbox;
    if (args.positionals.empty()) {
        emit_result(args, false, "verify: requires <file>");
        return exit_code::USAGE;
    }
    auto file_path = path::expand_home(args.positionals[0]);
    if (!io::file_exists(file_path)) {
        emit_result(args, false, "file not found: " + file_path.string());
        return exit_code::NOINPUT;
    }

    auto reader = stfs::StfsReader::open(file_path, args.no_mmap);
    if (!reader.is_ok()) {
        emit_result(args, false, reader.error().message());
        return exit_code::DATAERR;
    }

    auto report = reader.value().verify_all_blocks();
    if (!report.is_ok()) {
        emit_result(args, false, report.error().message());
        return exit_code::DATAERR;
    }

    if (args.json_output) {
        std::ostringstream oss;
        oss << "{\"ok\":" << (report.value().failed == 0 ? "true" : "false")
            << ",\"total_blocks\":" << report.value().total_blocks
            << ",\"verified_ok\":" << report.value().verified_ok
            << ",\"failed\":" << report.value().failed
            << ",\"failed_blocks\":[";
        for (std::size_t i = 0; i < report.value().failed_block_indices.size(); ++i) {
            if (i) oss << ",";
            oss << report.value().failed_block_indices[i];
        }
        oss << "]}";
        emit_json(args, oss.str());
    } else {
        std::cout << "Verification: " << report.value().verified_ok << "/"
                  << report.value().total_blocks << " blocks OK";
        if (report.value().failed > 0) {
            std::cout << " (" << report.value().failed << " FAILED)\n";
            std::cout << "First failing blocks:\n";
            for (std::size_t i = 0; i < std::min<std::size_t>(10, report.value().failed_block_indices.size()); ++i) {
                std::cout << "  block " << report.value().failed_block_indices[i] << "\n";
            }
        } else {
            std::cout << " - all OK\n";
        }
    }
    return (report.value().failed == 0) ? exit_code::OK : exit_code::DATAERR;
}

// ---------------------------------------------------------------------------
// disable command
// ---------------------------------------------------------------------------
int cmd_disable(const ParsedArgs& args) {
    using namespace xbox;
    if (args.positionals.size() < 2) {
        emit_result(args, false, "disable: requires <title_id> <file_name>");
        return exit_code::USAGE;
    }
    auto tid = str::parse_hex_u32(args.positionals[0]);
    if (!tid) {
        emit_result(args, false, "invalid title ID: " + args.positionals[0]);
        return exit_code::USAGE;
    }
    auto resolver = make_resolver(args);
    auto xuid_filter = parse_xuid_filter(args);
    auto r = installer::disable_package(*tid, args.positionals[1], resolver, xuid_filter);
    if (!r.is_ok()) {
        emit_result(args, false, r.error().message());
        return exit_code::DATAERR;
    }
    emit_result(args, true, "Disabled: " + r.value().string());
    return exit_code::OK;
}

// ---------------------------------------------------------------------------
// enable command
// ---------------------------------------------------------------------------
int cmd_enable(const ParsedArgs& args) {
    using namespace xbox;
    if (args.positionals.size() < 2) {
        emit_result(args, false, "enable: requires <title_id> <file_name>");
        return exit_code::USAGE;
    }
    auto tid = str::parse_hex_u32(args.positionals[0]);
    if (!tid) {
        emit_result(args, false, "invalid title ID: " + args.positionals[0]);
        return exit_code::USAGE;
    }
    auto resolver = make_resolver(args);
    auto xuid_filter = parse_xuid_filter(args);
    auto r = installer::enable_package(*tid, args.positionals[1], resolver, xuid_filter);
    if (!r.is_ok()) {
        emit_result(args, false, r.error().message());
        return exit_code::DATAERR;
    }
    emit_result(args, true, "Enabled: " + r.value().string());
    return exit_code::OK;
}

// ---------------------------------------------------------------------------
// search command
// ---------------------------------------------------------------------------
int cmd_search(const ParsedArgs& args) {
    using namespace xbox;
    if (args.positionals.empty()) {
        emit_result(args, false, "search: requires <query>");
        return exit_code::USAGE;
    }
    const auto& query = args.positionals[0];
    auto resolver = make_resolver(args);
    auto xuid_filter = parse_xuid_filter(args);
    auto entries = resolver.list_all_installed();
    if (!entries.is_ok()) {
        emit_result(args, false, entries.error().message());
        return exit_code::IOERR;
    }

    std::string filter_desc;
    if (xuid_filter) {
        filter_desc = " (Profile: " + format_xuid_short(*xuid_filter) + ")";
    }

    std::vector<installer::PathResolver::InstalledEntry> matches;
    for (const auto& e : entries.value()) {
        if (!xuid_matches(e, xuid_filter)) continue;

        std::string tid_str = str::format_hex_u32(e.title_id);
        std::string ct_str = str::format_hex_u32(e.content_type);
        auto header_path = compute_header_path(e);
        auto display_name = read_header_display_name(header_path);
        std::string ct_display = content_type_display_name(e.content_type);
        std::string xuid_str = format_xuid_short(e.xuid);

        std::string query_lower = str::to_lower(std::string(query));
        std::string tid_lower = str::to_lower(tid_str);
        std::string ct_lower = str::to_lower(ct_str);
        std::string fn_lower = str::to_lower(e.file_name);
        std::string dn_lower = str::to_lower(display_name);
        std::string ctd_lower = str::to_lower(ct_display);
        std::string xuid_lower = str::to_lower(xuid_str);

        if (tid_lower.find(query_lower) != std::string::npos ||
            ct_lower.find(query_lower) != std::string::npos ||
            fn_lower.find(query_lower) != std::string::npos ||
            dn_lower.find(query_lower) != std::string::npos ||
            ctd_lower.find(query_lower) != std::string::npos ||
            xuid_lower.find(query_lower) != std::string::npos) {
            matches.push_back(e);
        }
    }

    if (args.json_output) {
        std::ostringstream oss;
        oss << "{\"query\":" << json_escape(query) << ",\"matches\":[";
        for (std::size_t i = 0; i < matches.size(); ++i) {
            const auto& e = matches[i];
            if (i) oss << ",";
            auto header_path = compute_header_path(e);
            auto display_name = read_header_display_name(header_path);
            oss << "{\"title_id\":\"" << str::format_hex_u32(e.title_id) << "\""
                << ",\"content_type\":\"" << str::format_hex_u32(e.content_type) << "\""
                << ",\"content_type_name\":" << json_escape(content_type_display_name(e.content_type))
                << ",\"display_name\":" << json_escape(display_name)
                << ",\"file_name\":" << json_escape(e.file_name)
                << ",\"path\":" << json_escape(e.path.string())
                << ",\"disabled\":" << (e.is_disabled ? "true" : "false")
                << "}";
        }
        oss << "]}";
        emit_json(args, oss.str());
    } else {
        if (matches.empty()) {
            std::cout << "No matches for '" << query << "'.\n";
        } else {
            std::cout << "Found " << matches.size() << " match(es):\n";
            std::cout << "─────────────────────────────────────────────\n";
            for (const auto& e : matches) {
                auto header_path = compute_header_path(e);
                auto display_name = read_header_display_name(header_path);

                std::cout << "  [" << content_type_display_name(e.content_type) << "] ";
                if (!display_name.empty()) {
                    std::cout << display_name;
                } else {
                    std::cout << e.file_name;
                }
                if (e.is_disabled) std::cout << "  (DISABLED)";
                std::cout << "\n";
                std::cout << "    Title: " << str::format_hex_u32(e.title_id) << "\n";
                std::cout << "    ID: " << e.file_name << "\n";
                std::cout << "    Path: " << e.path.string() << "\n\n";
            }
        }
    }
    return exit_code::OK;
}

// ---------------------------------------------------------------------------
// saves command - List all save files (content_type = 0x00000001)
// ---------------------------------------------------------------------------
int cmd_saves(const ParsedArgs& args) {
    using namespace xbox;
    auto resolver = make_resolver(args);
    auto xuid_filter = parse_xuid_filter(args);

    std::string filter_desc;
    if (xuid_filter) {
        filter_desc = " (Profile: " + format_xuid_short(*xuid_filter) + ")";
    }

    // If a title_id is provided, list saves for that title only
    if (!args.positionals.empty()) {
        auto tid = str::parse_hex_u32(args.positionals[0]);
        if (!tid) {
            emit_result(args, false, "invalid title ID: " + args.positionals[0]);
            return exit_code::USAGE;
        }
        auto entries = resolver.list_installed_for_title(*tid);
        if (!entries.is_ok()) {
            emit_result(args, false, entries.error().message());
            return exit_code::IOERR;
        }

        // Filter to only save files (content_type = 0x00000001) and apply XUID filter
        std::vector<const installer::PathResolver::InstalledEntry*> saves;
        for (const auto& e : entries.value()) {
            if (e.content_type != 0x00000001) continue;
            if (!xuid_matches(e, xuid_filter)) continue;
            saves.push_back(&e);
        }

        if (args.json_output) {
            std::ostringstream oss;
            oss << "{\"title_id\":\"" << str::format_hex_u32(*tid) << "\",\"saves\":[";
            for (std::size_t i = 0; i < saves.size(); ++i) {
                if (i) oss << ",";
                const auto* e = saves[i];
                auto header_path = compute_header_path(*e);
                auto display_name = read_header_display_name(header_path);
                oss << "{\"file_name\":" << json_escape(e->file_name)
                    << ",\"display_name\":" << json_escape(display_name)
                    << ",\"xuid\":\"" << format_xuid_short(e->xuid) << "\""
                    << ",\"disabled\":" << (e->is_disabled ? "true" : "false")
                    << "}";
            }
            oss << "]}";
            emit_json(args, oss.str());
        } else {
            if (saves.empty()) {
                std::cout << "No save files for title " << str::format_hex_u32(*tid)
                          << filter_desc << "\n";
            } else {
                std::cout << "Save files for title " << str::format_hex_u32(*tid)
                          << filter_desc << ":\n";
                std::cout << "═════════════════════════════════════════════\n";
                for (const auto* e : saves) {
                    auto header_path = compute_header_path(*e);
                    auto display_name = read_header_display_name(header_path);

                    std::cout << "  📁 ";
                    if (!display_name.empty()) {
                        std::cout << display_name;
                    } else {
                        std::cout << e->file_name;
                    }
                    if (e->is_disabled) std::cout << "  (DISABLED)";
                    std::cout << "\n";
                    std::cout << "    Profile: " << format_xuid_short(e->xuid) << "\n";
                    std::cout << "    ID: " << e->file_name << "\n";
                    std::cout << "    Path: " << e->path.string() << "\n\n";
                }
            }
        }
        return exit_code::OK;
    }

    // No title_id: list all saves across all titles
    auto titles = resolver.list_installed_titles();
    if (!titles.is_ok()) {
        emit_result(args, false, titles.error().message());
        return exit_code::IOERR;
    }

    // Collect all saves
    struct SaveInfo {
        u32 title_id;
        std::string file_name;
        std::string display_name;
        u64 xuid;
        std::string path;
        bool is_disabled;
    };
    std::vector<SaveInfo> all_saves;

    for (auto tid : titles.value()) {
        auto entries = resolver.list_installed_for_title(tid);
        if (!entries.is_ok()) continue;

        for (const auto& e : entries.value()) {
            if (e.content_type != 0x00000001) continue;
            if (!xuid_matches(e, xuid_filter)) continue;
            if (e.is_disabled && !args.flags.contains("disabled")) continue;

            auto header_path = compute_header_path(e);
            auto display_name = read_header_display_name(header_path);

            all_saves.push_back({
                tid, e.file_name, display_name, e.xuid,
                e.path.string(), e.is_disabled
            });
        }
    }

    if (args.json_output) {
        std::ostringstream oss;
        oss << "{\"saves\":[";
        for (std::size_t i = 0; i < all_saves.size(); ++i) {
            if (i) oss << ",";
            const auto& s = all_saves[i];
            oss << "{\"title_id\":\"" << str::format_hex_u32(s.title_id) << "\""
                << ",\"file_name\":" << json_escape(s.file_name)
                << ",\"display_name\":" << json_escape(s.display_name)
                << ",\"xuid\":\"" << format_xuid_short(s.xuid) << "\""
                << ",\"path\":" << json_escape(s.path)
                << ",\"disabled\":" << (s.is_disabled ? "true" : "false")
                << "}";
        }
        oss << "]}";
        emit_json(args, oss.str());
    } else {
        std::cout << "All save files" << filter_desc << ":\n";
        std::cout << "═════════════════════════════════════════════\n";

        if (all_saves.empty()) {
            std::cout << "  No save files found" << filter_desc << ".\n";
        } else {
            for (const auto& s : all_saves) {
                std::cout << "  📁 ";
                if (!s.display_name.empty()) {
                    std::cout << s.display_name;
                } else {
                    std::cout << s.file_name;
                }
                if (s.is_disabled) std::cout << "  (DISABLED)";
                std::cout << "\n";
                std::cout << "    Game: " << str::format_hex_u32(s.title_id) << "\n";
                std::cout << "    Profile: " << format_xuid_short(s.xuid) << "\n";
                std::cout << "    ID: " << s.file_name << "\n\n";
            }
            std::cout << "Total: " << all_saves.size() << " save file(s)\n";
        }
    }
    return exit_code::OK;
}

// ---------------------------------------------------------------------------
// export-save command - Export a save file to a directory
// Exports with full Xenia directory structure:
//   <output_dir>/<title_id>/00000001/<file_name>/(files)
//   <output_dir>/<title_id>/Headers/00000001/<file_name>.header
//
// Smart path handling: if the user didn't quote a path with spaces,
// all positional args after file_name are joined to form the output path.
// ---------------------------------------------------------------------------
int cmd_export_save(const ParsedArgs& args) {
    using namespace xbox;
    if (args.positionals.size() < 3) {
        emit_result(args, false, "export-save: requires <title_id> <file_name> <output_dir>");
        return exit_code::USAGE;
    }
    auto tid = str::parse_hex_u32(args.positionals[0]);
    if (!tid) {
        emit_result(args, false, "invalid title ID: " + args.positionals[0]);
        return exit_code::USAGE;
    }

    // file_name is positionals[1]
    std::string file_name = args.positionals[1];

    // output_dir: join all remaining positionals with spaces
    // This handles unquoted paths with spaces like:
    //   /run/media/user/disk/xbox 360 saves/backup/
    std::string output_dir_str;
    for (std::size_t i = 2; i < args.positionals.size(); ++i) {
        if (!output_dir_str.empty()) output_dir_str += " ";
        output_dir_str += args.positionals[i];
    }

    auto resolver = make_resolver(args);
    auto xuid_filter = parse_xuid_filter(args);

    // Find the installed save (with optional --xuid filter for multi-profile saves)
    xbox::installer::PathResolver::InstalledEntry entry;
    if (xuid_filter) {
        auto entry_r = resolver.find_installed(*tid, file_name, *xuid_filter);
        if (!entry_r.is_ok()) {
            emit_result(args, false, entry_r.error().message());
            return exit_code::DATAERR;
        }
        entry = std::move(entry_r).value();
    } else {
        auto entry_r = resolver.find_installed(*tid, file_name);
        if (!entry_r.is_ok()) {
            emit_result(args, false, entry_r.error().message());
            return exit_code::DATAERR;
        }
        entry = std::move(entry_r).value();
    }

    // Allow export of any content type, not just saves
    // (user might want to export DLC or TU too)

    fs::path output_base = path::expand_home(output_dir_str);

    // Build the full Xenia structure:
    // <output_dir>/<title_id>/<content_type>/<file_name>/
    // <output_dir>/<title_id>/Headers/<content_type>/<file_name>.header
    std::string title_id_str = str::format_hex_u32(*tid);
    std::string ct_str = str::format_hex_u32(entry.content_type);

    fs::path save_dest = output_base / title_id_str / ct_str / entry.file_name;
    fs::path header_dest = output_base / title_id_str / "Headers" / ct_str / (entry.file_name + ".header");

    XBOX_LOG_INFO("Exporting '{}' to '{}'", entry.file_name, output_base.string());
    XBOX_LOG_INFO("Save destination: {}", save_dest.string());
    XBOX_LOG_INFO("Header destination: {}", header_dest.string());

    // Create directories
    std::error_code ec;
    fs::create_directories(save_dest, ec);
    if (ec) {
        emit_result(args, false, "failed to create save directory: " + ec.message());
        return exit_code::IOERR;
    }
    fs::create_directories(header_dest.parent_path(), ec);

    // Copy all files from the save directory
    int file_count = 0;
    u64 total_bytes = 0;
    for (auto& file_entry : fs::recursive_directory_iterator(entry.path, ec)) {
        if (!file_entry.is_regular_file(ec)) continue;

        auto relative = fs::relative(file_entry.path(), entry.path, ec);
        if (ec) continue;

        auto dest = save_dest / relative;
        fs::create_directories(dest.parent_path(), ec);
        fs::copy_file(file_entry.path(), dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            XBOX_LOG_WARN("Failed to copy {}: {}", file_entry.path().string(), ec.message());
            continue;
        }
        ++file_count;
        total_bytes += fs::file_size(file_entry.path(), ec);
    }

    // Copy the .header file
    auto header_path = compute_header_path(entry);
    if (fs::exists(header_path, ec)) {
        fs::copy_file(header_path, header_dest, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            ++file_count;
            XBOX_LOG_INFO("Copied header file");
        }
    }

    // Print summary with the full structure
    std::cout << "═════════════════════════════════════════════\n";
    std::cout << "  Export: " << entry.file_name << "\n";
    std::cout << "  Title:  " << title_id_str << "\n";
    std::cout << "  Type:   " << content_type_display_name(entry.content_type) << "\n";
    std::cout << "  Files:  " << file_count << " (" << total_bytes << " bytes)\n";
    std::cout << "  To:     " << output_base.string() << "\n";
    std::cout << "═════════════════════════════════════════════\n";
    std::cout << "\nExported structure:\n";
    std::cout << "  " << title_id_str << "/\n";
    std::cout << "  ├── " << ct_str << "/\n";
    std::cout << "  │   └── " << entry.file_name << "/\n";
    std::cout << "  │       └── (" << file_count - 1 << " files)\n";
    std::cout << "  └── Headers/\n";
    std::cout << "      └── " << ct_str << "/\n";
    std::cout << "          └── " << entry.file_name << ".header\n";
    return exit_code::OK;
}

// ---------------------------------------------------------------------------
// dispatch
// ---------------------------------------------------------------------------
int dispatch(ParsedArgs args) {
    configure_logger(args);

    if (args.version_requested) {
        std::cout << format_version();
        return exit_code::OK;
    }
    if (args.help_requested || args.command.empty()) {
        std::cout << format_help(args.command);
        return exit_code::OK;
    }

    // Resolve command aliases
    std::string cmd = args.command;
    if (cmd == "i" || cmd == "add") cmd = "install";
    else if (cmd == "rm" || cmd == "remove") cmd = "uninstall";
    else if (cmd == "ls") cmd = "list";
    else if (cmd == "inspect" || cmd == "show") cmd = "info";
    else if (cmd == "check") cmd = "verify";
    else if (cmd == "find") cmd = "search";

    if (cmd == "install")        return cmd_install(args);
    if (cmd == "uninstall")      return cmd_uninstall(args);
    if (cmd == "list")           return cmd_list(args);
    if (cmd == "info")           return cmd_info(args);
    if (cmd == "verify")         return cmd_verify(args);
    if (cmd == "disable")        return cmd_disable(args);
    if (cmd == "enable")         return cmd_enable(args);
    if (cmd == "search")         return cmd_search(args);
    if (cmd == "saves")          return cmd_saves(args);
    if (cmd == "export-save")    return cmd_export_save(args);

    emit_result(args, false, "Unknown command: " + args.command + "\n\n" + format_help(""));
    return exit_code::USAGE;
}

} // namespace xbox::cli
