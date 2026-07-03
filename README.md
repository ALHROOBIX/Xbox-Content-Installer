# xbox-360-installation

A cross-platform command-line tool for installing Xbox 360 content — **Title Updates (TU)**, **Downloadable Content (DLC)**, **Save Files**, and **ISO (XISO) game images** — in a layout 100% compatible with **Xenia Canary**.

Built with modern C++20 with strict layer separation, atomic file extraction, SHA1 integrity verification, parallel installation via a custom Thread Pool, and full support for STFS, XISO, and ZIP formats.

---

## ✅ Tested on Real Xbox 360 Files

The tool has been tested on actual Xbox 360 files:

| File | Type | Size | Block Count | Extracted Files | Verification |
|------|------|------|-------------|-----------------|--------------|
| Minecraft Title Update | TU | 3.0 MB | 739 | 4 | ✅ 739/739 OK |
| Adventure Time DLC | DLC | 471 KB | 103 | 1 | ✅ 103/103 OK |
| Angry Birds Texture Pack | DLC | 6.5 MB | 1585 | 3 | ✅ 1585/1585 OK |
| Minecraft Save (skyblock) | Save (CON) | 1.6 MB | 372 | 1 | ✅ 372/372 OK |
| GTA IV Save (SGTA400) | Save (CON) | 790 KB | 177 | 1 | ✅ 177/177 OK |
| Splatterhouse Save (ZIP→CON) | Save (CON) | 68 KB | 5 | 1 | ✅ 5/5 OK |
| Splatterhouse (USA) ISO | ISO (XISO) | 7.6 GB | 3827488 | 10820 | ✅ default.xex found |

**Performance:** 143ms to install 3 files (147.7 MB/s). ISO extraction: streaming 1MB buffer, ~0 RAM overhead.

---

## 🆕 New Features in v4.0

### 1. Save File Support
- Automatic installation of CON (Saved Game) files
- Generates `.header` files byte-identical to Xenia (verified byte-by-byte)
- Reads `save_game_id` and `display_name` from the STFS header
- Supports ZIP files from Xenia Manager

### 2. Smart ZIP Type Detection
The tool automatically detects the ZIP file type and handles it accordingly:
- **Type 1:** ZIP containing a CON/LIVE/PIRS file ← extracts and installs as a full STFS package
- **Type 2:** ZIP from Xenia Manager (directories + .header) ← extracts to the correct path
- **Type 3:** ZIP with extracted folder without .header ← extracts and infers information

### 3. Profile Support (`--xuid`)
- Specify the target profile for saves: `--xuid 0000000000000000`
- Without `--xuid`, reads `profile_id` from the original save file
- DLC and TU are automatically installed under `0000000000000000` (shared profile)

### 4. Automatic Content Root Path Saving
- When `--content-root` is used once, the path is saved
- Subsequent commands (`list`, `uninstall`, `saves`) work without `--content-root`
- Path saved at: `~/.config/xbox-install/config.txt` (Linux)

### 5. New Commands
- `saves` — list save files only (with `--json` support)
- `export-save` — export a save file to a backup directory
- `search` — search installed content by game name
- `--type` filter for `list` command (`tu` / `dlc` / `saves` / `all`)

### 6. `--xuid` Support Across All Commands
The `--xuid` flag is now supported in: `install`, `uninstall`, `disable`, `enable`, `export-save`, `list`, `saves`. This is essential for managing saves across multiple profiles.

### 7. ISO (XISO) Support
- Full XISO disc image reading (based on XGDTool architecture)
- `info` command shows ISO details: root sector, total files, executable name
- `install` extracts all files including `default.xex` (requires `--extract-svod` flag)
- `list` recognizes ISO packages with 💿 icon
- Streaming extraction (1MB buffer) — low memory, high speed
- Supports XGD1/XGD2/XGD3 disc image offsets

### 8. Fully Static Linux Binary
- All libraries (libstdc++, libgcc, zlib, pthread) statically linked
- Runs on ANY Linux x86_64 system (Ubuntu, Fedora, Mint, Arch, CentOS)
- No library dependency issues (`ldd` shows "not a dynamic executable")

---

## 📦 Installation

### From Source (Linux)

```bash
# Requirements: g++ (supports C++20), make, ar, minizip-dev, zlib-dev
# Fedora: sudo dnf install gcc-c++ minizip-devel zlib-devel
# Ubuntu: sudo apt install g++ libminizip-dev zlib1g-dev

git clone https://github.com/ALHROOBIX/Xbox-Content-Installer.git
cd xbox-360-installation
./scripts/build.sh
```

The binary will be at `build/xbox-install`.

### From Source (Windows Cross-Compile from Linux)

```bash
# Requirements: mingw-w64, zlib (for cross-compile)
# Fedora: sudo dnf install mingw64-gcc-c++ mingw64-zlib
# Ubuntu: sudo apt install g++-mingw-w64-x86-64-posix

git clone https://github.com/ALHROOBIX/Xbox-Content-Installer.git
cd xbox-360-installation
./scripts/build.sh --windows
```

The binary will be at `build-win/xbox-install.exe`.

### Pre-built Binaries

Pre-built binaries are available in the [Releases](../../releases) section:
- `xbox-install` — Linux x86_64
- `xbox-install.exe` — Windows x86_64 (with embedded manifest, no admin required)

---

## 🚀 Quick Start

### Install a Title Update
```bash
./xbox-install install TU_1C424FN_0000004000000.0000000000081 \
    --content-root "/path/to/xenia-canary/content/"
```

### Install DLC
```bash
./xbox-install install "Adventure Time DLC.zip" \
    --content-root "/path/to/xenia-canary/content/"
```

### Install a Save File (with specific profile)
```bash
./xbox-install install SGTA400 \
    --content-root "/path/to/xenia-canary/content/" \
    --xuid E030000018BED309
```

### Install an ISO Game (requires --extract-svod)
```bash
./xbox-install install "Splatterhouse.iso" \
    --content-root "/path/to/xenia-canary/content/" \
    --extract-svod
```

### View Package Info (STFS or ISO)
```bash
./xbox-install info TU_1C424FN_0000004000000.0000000000081
./xbox-install info "game.iso"
```

### List All Installed Content
```bash
./xbox-install list --content-root "/path/to/xenia-canary/content/"
```

### List by Type
```bash
./xbox-install list --content-root "/path/to/xenia-canary/content/" --type tu
./xbox-install list --content-root "/path/to/xenia-canary/content/" --type dlc
./xbox-install list --content-root "/path/to/xenia-canary/content/" --type saves
```

### Uninstall
```bash
# Uninstall TU/DLC (shared content)
./xbox-install uninstall 5454082B TU_1C424FN_0000004000000 \
    --content-root "/path/to/xenia-canary/content/"

# Uninstall save from a specific profile
./xbox-install uninstall 545407F2 SGTA400 \
    --content-root "/path/to/xenia-canary/content/" \
    --xuid E030000018BED309
```

### Export a Save (Backup)
```bash
./xbox-install export-save 545407F2 SGTA400 "/path/to/backup/" \
    --content-root "/path/to/xenia-canary/content/" \
    --xuid E030000018BED309
```

---

## 📋 Commands Reference

### Global Options
| Option | Description |
|--------|-------------|
| `--content-root <path>` | Override the content root directory |
| `--xuid <hex>` | Filter by profile XUID (16-char hex) |
| `--json` | Output in JSON format |
| `--no-mmap` | Disable memory-mapped I/O (use for FUSE/NTFS) |
| `--extract-svod` | Extract ISO files (required for ISO install) |
| `-v` / `-vv` / `-vvv` | Verbose / debug / trace logging |
| `-h` / `--help` | Show help |
| `-V` / `--version` | Show version |

### Commands

#### `install <file>` — Install a package
Installs a TU, DLC, save file, or ISO game. Supports STFS (CON/LIVE/PIRS), XISO, and ZIP.

```bash
xbox-install install [options] <file>
```

**For STFS (TU/DLC/Saves):**
- No special flags needed
- `--xuid` for profile-specific saves

**For ISO:**
- **`--extract-svod` is required!**
- Without it, you get an error message with usage instructions
- Extracts all files including `default.xex`

**Options:**
- `--content-root <path>` — Target content directory
- `--xuid <hex>` — Target profile (for saves)
- `--extract-svod` — **Required for ISO files**
- `--no-verify` — Skip SHA1 verification
- `--no-mmap` — Use buffer reading instead of mmap
- `--allow-unknown` — Allow unknown content types
- `--dry-run` — Show what would be installed without writing
- `-j N` / `--threads N` — Number of parallel workers
- `--overwrite` — Overwrite existing installation
- `--skip-existing` — Skip if already installed

#### `uninstall <title_id> <file_name>` — Uninstall a package
Removes the package directory and its `.header` file.

```bash
xbox-install uninstall [options] <title_id> <file_name>
```

**Options:**
- `--content-root <path>` — Content directory
- `--xuid <hex>` — Profile filter (for saves across multiple profiles)

#### `list [title_id]` — List installed content
Lists all installed titles and their packages.

```bash
xbox-install list [options] [title_id]
```

**Options:**
- `--content-root <path>` — Content directory
- `--xuid <hex>` — Filter by profile
- `--type <tu|dlc|saves|all>` — Filter by content type
- `--disabled` — Show disabled packages
- `--json` — JSON output

#### `saves [title_id]` — List save files only
```bash
xbox-install saves [options] [title_id]
```

**Options:** Same as `list`, plus `--json`.

#### `info <file>` — Show package information
Displays STFS header info or XISO disc info without installing.

```bash
xbox-install info [options] <file>
```

Works for both STFS files and ISO files.

#### `verify <file>` — Verify SHA1 integrity
Verifies block hashes (file-chain only, matches Xenia behavior).

```bash
xbox-install verify [options] <file>
```

#### `disable <title_id> <file_name>` — Disable a package
Renames the package directory to `.disabled` (Xenia ignores it).

```bash
xbox-install disable [options] <title_id> <file_name>
```

**Options:**
- `--xuid <hex>` — Profile filter

#### `enable <title_id> <file_name>` — Re-enable a package
```bash
xbox-install enable [options] <title_id> <file_name>
```

**Options:**
- `--xuid <hex>` — Profile filter

#### `search <query>` — Search installed content
Searches by game name or title ID.

```bash
xbox-install search [options] <query>
```

#### `export-save <title_id> <file_name> <output_dir>` — Export a save
Exports a save file with the full Xenia directory structure.

```bash
xbox-install export-save [options] <title_id> <file_name> <output_dir>
```

**Options:**
- `--xuid <hex>` — Profile filter (required if saves exist under multiple profiles)

---

## 📁 Directory Layout

The tool produces a layout 100% identical to Xenia Canary:

```
content/
├── 0000000000000000/              ← Shared content (TU, DLC)
│   └── 4D5307D5/                  ← Title ID (Gears of War)
│       ├── 000B0000/              ← Content Type (TU = 0x000B0000)
│       │   └── TU_16L61UL_0000004000000.0000000000181/
│       │       ├── default.xexp
│       │       ├── DLC_Annex.xxx
│       │       └── ... (36 files)
│       └── Headers/
│           └── 000B0000/
│               └── TU_16L61UL_0000004000000.0000000000181.header
│
└── E030000018BED309/              ← Profile-specific (saves)
    └── 545407F2/                  ← Title ID (GTA IV)
        ├── 00000001/              ← Content Type (Save = 0x00000001)
        │   └── SGTA400/
        │       └── savegame.dat
        └── Headers/
            └── 00000001/
                └── SGTA400.header
```

### ISO — Extracted Files
```
content/
└── 0000000000000000/
    └── 00000000/
        └── 00007000/
            └── game.iso/          ← Named after the ISO file
                ├── default.xex    ← Main executable
                ├── $SystemUpdate/
                ├── data/
                │   └── ...
                └── ...
```

### Content Types
| Hex | Name | Description |
|-----|------|-------------|
| `0x000B0000` | Title Update (TU) | Game patches/updates |
| `0x00000002` | DLC / Marketplace | Downloadable content |
| `0x00000001` | Saved Game | Save files (per-profile) |
| `0x00007000` | Xbox 360 Title | Full game |
| `0x000D0000` | Arcade Title | Xbox Live Arcade game |
| `0x00040000` | Game Demo | Demo versions |

---

## 🔧 Technical Details

### STFS Container Format
- Parses CON/LIVE/PIRS containers with exact Xenia-compatible offset calculations
- `BlockToOffset` formula matches Xenia source code exactly
- Volume descriptor parsing with correct endianness (mixed LE/BE fields)
- 3-level hash table with proper block chain following

### SHA1 Verification (Xenia-Compatible)
- Verifies only blocks in file chains (not free/unused blocks)
- On mismatch, logs a warning and continues (matches Xenia behavior)
- Does NOT abort installation on hash failure (Xenia doesn't verify at all)

### Memory Management
- Memory-mapped I/O with automatic fallback for FUSE/NTFS filesystems
- Thread pool with bounded queue for parallel extraction
- Atomic file writes (temp file + rename)

### Windows Build
- Cross-compiled via MinGW-w64 with static linking (no DLL dependencies)
- Embedded application manifest (`asInvoker` — no admin required)
- VS_VERSION_INFO resource with proper metadata
- Wide-char path support (`_wfopen`) for Unicode filenames
- DEP + ASLR + High-Entropy VA enabled

### XISO (ISO) Support
- Based on XGDTool architecture (GoDReader + XisoReader)
- Reads XISO directory tree using BFS (non-recursive, no stack overflow)
- Supports XGD1/XGD2/XGD3 disc image offsets
- Streaming extraction (1MB buffer) — minimal RAM usage
- Automatically finds `default.xex` executable

### Linux Static Build
- All libraries statically linked (libstdc++, libgcc, zlib, pthread, dl)
- `ldd` shows "not a dynamic executable" — zero dependencies
- Runs on ANY Linux x86_64 (Ubuntu, Fedora, Mint, Arch, CentOS)

---

## 🏗️ Build System

### Build Targets
```bash
./scripts/build.sh              # Linux native
./scripts/build.sh --windows    # Windows cross-compile
./scripts/build.sh --tests      # Linux + tests
./scripts/build.sh --all        # Linux + Windows + tests
```

### Dependencies
- **C++20 compiler** (g++ 12+, clang++ 14+, or MSVC 2022)
- **zlib** — compression library
- **minizip** — ZIP archive support (bundled for Windows)
- **pthreads** — threading (Linux/macOS)
- **winpthread** — threading (Windows, statically linked)

---

## 🧪 Testing

The project includes a comprehensive test suite:

```bash
./scripts/build.sh --tests
./build/test_xbox_core
```

| Test Suite | Tests | Coverage |
|------------|-------|----------|
| test_endianness | 8 | Byte order conversions |
| test_sha1 | 13 | SHA-1 hash computation |
| test_stfs_header | 15 | STFS header parsing |
| test_stfs_path_resolver | 12 | Path resolution |
| test_volume_descriptor | 9 | Volume descriptor parsing |
| test_svod | 13 | SVOD container support |
| test_string_utils | 11 | String utilities |
| test_thread_pool | 8 | Thread pool behavior |

---

## 📚 References and Learning Resources

- [Xenia Canary source](https://github.com/xenia-canary/xenia-canary) — Primary reference (`canary_experimental` branch)
- [XGDTool](https://github.com/wiredopposite/XGDTool) — XISO/GOD format reference and architecture
- [Free60 STFS wiki](https://free60.org/STFS) — STFS format documentation
- [arkem.org PDF](https://www.arkem.org/xbox360-file-reference.pdf) — Xbox 360 file format reference
- [FIPS 180-4](https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf) — SHA-1 specification
- [GNU GPL v3](https://www.gnu.org/licenses/gpl-3.0.html) — Full license text
- [SPDX License List](https://spdx.org/licenses/) — Standard license identifiers
- [MinGW-w64](https://www.mingw-w64.org/) — Windows cross-compilation toolchain
- [zlib](https://zlib.net/) — Compression library
- [minizip](https://github.com/madler/zlib/tree/master/contrib/minizip) — ZIP utility for zlib

---

## 📄 License

This project is licensed under the **GNU General Public License v3.0 or later** (GPL-3.0-or-later).

```
xbox-360-installation — Cross-platform Xbox 360 content installer for Xenia Canary
Copyright (C) 2026 ALHROOBIX

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
```

### 🔒 Your Rights Under GPL v3

| Right | Description |
|-------|-------------|
| **Use** | Free to use the software for any purpose |
| **Study** | Free to study and modify the source code |
| **Distribute** | Free to redistribute copies |
| **Improve** | Free to improve the program and release improvements |

**Distribution Obligations:** If you distribute modified copies, you must:
1. Include the complete source code
2. Keep the same GPL v3 license
3. Document your changes
4. Preserve original copyright notices

### 🛡️ Disclaimer

The software is provided "as is" without any warranty. See sections 15 and 16 of the GPL v3 license.

See the `LICENSE` file for the full license text, and `SPDX-License-Identifier: GPL-3.0-or-later` in each source file header.

---

## 👤 Author

**ALHROOBIX** — 2026

- GitHub: [@ALHROOBIX](https://github.com/ALHROOBIX)
- License: GPL-3.0-or-later

---

## 🙏 Acknowledgments

- [Xenia Project](https://github.com/xenia-project) — For the amazing Xbox 360 emulator
- [XGDTool](https://github.com/wiredopposite/XGDTool) — XISO/GOD format reference
- [Free60](https://free60.org/) — For Xbox 360 hardware/software documentation
- [zlib](https://zlib.net/) — For the compression library
- [MinGW-w64](https://www.mingw-w64.org/) — For the cross-compilation toolchain
